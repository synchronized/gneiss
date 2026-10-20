// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

// 独立接入验证：不链接 Engine DLL，不将 CAORS 对象带入公共 SDK。
#include "engine/core/tasks/task_scheduler.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"
#include <caors/assets.h>
#include <caors/io.h>
#include <caors/tasks.h>
#include <yyjson.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using caors::errc;
using caors::error;
using caors::error_domain;
using gneiss::result;

error failure(errc value) { return {error_domain::resources, value}; }

// 每个适配器只占用宿主的一个 scope，不创建第二套线程池。
class worker_context final : public caors::tasks::external_executor_context {
public:
  explicit worker_context(gneiss::tasks::task_executor& executor)
      : executor_(executor), scope_(executor.make_scope()) {}
  error post(caors::tasks::task task) override {
    const std::scoped_lock lock(mutex_);
    if (closed_)
      return failure(errc::frozen);
    auto owned = std::make_shared<caors::tasks::task>(std::move(task));
    gneiss::tasks::task_handle handle;
    const auto status = executor_.submit(
        {.name = "caors", .scope = scope_},
        [owned](const auto&) {
          (void)(*owned)();
          return gneiss::tasks::task_outcome{};
        },
        handle);
    return status == gneiss::tasks::submit_result::success ? error{} : failure(errc::frozen);
  }
  // 只能由宿主线程调用；任务内关闭由宿主协议禁止。
  void close() noexcept override {
    {
      const std::scoped_lock lock(mutex_);
      closed_ = true;
    }
    (void)executor_.close_scope(scope_);
  }
  void collect() {
    std::vector<gneiss::tasks::task_completion> values;
    (void)executor_.poll(scope_, values);
  }

private:
  gneiss::tasks::task_executor& executor_;
  gneiss::tasks::task_scope scope_;
  std::mutex mutex_;
  bool closed_{};
};

struct json_asset final : caors::resource {
  using caors::resource::resource;
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> document{nullptr, yyjson_doc_free};
};

// 过渡接法：CAORS 捕获并解析唯一来源；Loader 复用现有 libuv 回执。
// pump 只轮询已提交的回执，不执行磁盘 I/O 或解析，不阻塞 worker 等待 I/O。
// 完整 async_file 后端与显式关闭协议留待下一阶段，不伪装成已实现。
class json_loader final : public caors::resources::async_loader<json_asset> {
  struct operation {
    caors::resources::async_load_context context;
    caors::resources::load_completion<json_asset> completion;
    std::unique_ptr<gneiss::io_internal::uv_file_open_operation> opening;
    std::shared_ptr<gneiss::io_internal::uv_async_file> file;
    std::unique_ptr<gneiss::io_internal::uv_file_read_operation> reading;
  };

public:
  explicit json_loader(gneiss::io_internal::uv_file_io& io) : io_(io) {}
  error start(caors::resources::async_load_context context,
              caors::resources::load_completion<json_asset> completion) override {
    if (!context.sources)
      return failure(errc::invalid_argument);
    auto resolved = context.sources->resolve(context.guid);
    if (!resolved)
      return resolved.error();
    // 此 probe 仅注册无根目录的 native 后端，目标路径已由 CAORS 路由为绝对路径。
    auto op = std::make_shared<operation>();
    op->context = std::move(context);
    op->completion = std::move(completion);
    const auto status = io_.begin_open(resolved.value.path.string_view(), op->opening);
    if (status != result::success) {
      (void)op->completion.complete({translate(status), {}});
      return {};
    }
    const std::scoped_lock lock(mutex_);
    pending_.push_back(std::move(op));
    ++starts;
    return {};
  }
  void pump() {
    const std::scoped_lock lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      const auto& op = *it;
      if (op->context.stop.stop_requested()) {
        (void)op->completion.complete({failure(errc::cancelled), {}});
        it = pending_.erase(it); // libuv 在途对象自行保活，宿主 loop 最后排空。
        continue;
      }
      result status = result::success;
      if (op->opening) {
        status = op->opening->poll(op->file);
        if (status == result::not_ready) {
          ++it;
          continue;
        }
        op->opening.reset();
        if (status == result::success) {
          if (op->file->size() > max_bytes)
            status = result::invalid_argument;
          else
            status =
                op->file->begin_read(0, static_cast<std::size_t>(op->file->size()), op->reading);
        }
      }
      std::span<const std::byte> bytes;
      if (status == result::success)
        status = op->reading->poll(bytes);
      if (status == result::not_ready) {
        ++it;
        continue;
      }
      if (status != result::success) {
        (void)op->completion.complete({translate(status), {}});
      } else {
        // 捕获回执保持借用字节有效；后台解析完成后才释放文件和缓冲。
        auto owned = op;
        const auto posted = op->context.worker.post(caors::tasks::task{[owned, bytes] {
          if (owned->context.stop.stop_requested()) {
            (void)owned->completion.complete({failure(errc::cancelled), {}});
            return;
          }
          try {
            auto value = caors::memory::make_unique<json_asset>(owned->context.resource,
                                                                owned->context.guid);
            value->document.reset(
                yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0));
            if (!value->document || !yyjson_is_obj(yyjson_doc_get_root(value->document.get()))) {
              (void)owned->completion.complete({failure(errc::invalid_argument), {}});
              return;
            }
            (void)owned->completion.complete({{}, std::move(value)});
          } catch (const std::bad_alloc&) {
            (void)owned->completion.complete({failure(errc::out_of_memory), {}});
          } catch (...) {
            (void)owned->completion.complete({failure(errc::backend_failure), {}});
          }
        }});
        if (posted)
          (void)op->completion.complete({posted, {}});
      }
      it = pending_.erase(it);
    }
  }
  bool empty() {
    const std::scoped_lock lock(mutex_);
    return pending_.empty();
  }
  // 调度器已排空后由宿主调用；回执释放不等待真实 I/O。
  void abandon() { pending_.clear(); }
  std::atomic_size_t starts{};
  static constexpr std::size_t max_bytes = 1024U * 1024U;

private:
  static error translate(result value) {
    if (value == result::not_found)
      return failure(errc::not_found);
    if (value == result::out_of_memory)
      return failure(errc::out_of_memory);
    if (value == result::invalid_argument)
      return failure(errc::invalid_argument);
    return failure(errc::backend_failure);
  }
  gneiss::io_internal::uv_file_io& io_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<operation>> pending_;
};

void require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

int run(const char* asset_path) {
  gneiss::io_internal::uv_loop_executor loop;
  require(loop.start() == result::success, "loop start");
  gneiss::io_internal::uv_file_io io(loop);
  gneiss::tasks::task_scheduler scheduler({.workers = 1});
  auto context = std::make_shared<worker_context>(scheduler);
  caors::tasks::external_executor worker(context);
  caors::tasks::execution_context completions; // 主线程队列，无新线程。
  caors::resources::cache<json_asset> cache;
  json_loader loader(io);
  caors::assets::asset_manager manager({worker.get_executor(), completions.get_executor()});
  struct cleanup {
    caors::tasks::external_executor& worker;
    json_loader& loader;
    caors::tasks::execution_context& completions;
    gneiss::io_internal::uv_loop_executor& loop;
    ~cleanup() {
      worker.close();
      loader.abandon();
      (void)completions.drain();
      if (loop.is_running())
        (void)loop.stop();
    }
  } guard{worker, loader, completions, loop};
  auto files = std::make_shared<caors::io::native_file_system>();
  auto router = std::make_shared<caors::io::file_system_resolver>();
  const auto path = std::filesystem::absolute(asset_path).generic_string();
  caors::resources::file_source source(router);
  auto guid = [](unsigned char index) {
    return caors::resource_guid{caors::guid::from_bytes(caors::guid::byte_array{index})};
  };
  // 使用 resolver 的 URI 转发，绝对目标由 target_prefix 指定。
  require(!router->mount("asset://document", files, path), "document mount");
  require(!manager.provider().bind(guid(1), source, "asset://document"), "bind");
  require(!manager.register_type(cache, loader, caors::string_name{"gneiss.probe.json"}),
          "register");
  auto wait = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
      loader.pump();
      (void)completions.drain();
      context->collect();
      require(std::chrono::steady_clock::now() < deadline, "timeout");
      std::this_thread::sleep_for(1ms); // 仅测试宿主节拍，不是 worker 等待 I/O。
    }
  };
  auto first = manager.request_load<json_asset>(guid(1));
  auto second = manager.request_load<json_asset>(guid(1));
  require(first.cancel(), "cancel subscription");
  wait([&] { return second.done(); });
  require(first.cancelled() && second.loaded(), "independent cancellation");
  require(loader.starts == 1, "deduplicate loading");
  require(yyjson_obj_size(yyjson_doc_get_root(second.try_get()->document.get())) > 0,
          "parsed real JSON");
  auto cached = manager.request_load<json_asset>(guid(1));
  require(cached.loaded() && cached.try_get().get() == second.try_get().get() && loader.starts == 1,
          "cache hit");
  require(!manager.provider().bind(guid(2), source, "asset://document/missing"), "missing bind");
  auto missing = manager.request_load<json_asset>(guid(2));
  wait([&] { return missing.done(); });
  require(missing.failed(), "missing fails");
  // 源码文件可读但不是 JSON，区分 I/O 成功与解析失败。
  require(!router->mount("asset://invalid", files,
                         std::filesystem::absolute(__FILE__).generic_string()),
          "invalid mount");
  require(!manager.provider().rebind(guid(1), source, "asset://invalid"), "invalid rebind");
  auto invalid = manager.request_reload<json_asset>(guid(1));
  wait([&] { return invalid.done(); });
  require(invalid.failed(), "invalid JSON fails");
  require(manager.find<json_asset>(guid(1)) == second.try_get().get(),
          "failed reload keeps cached resource");
  require(!manager.provider().rebind(guid(1), source, "asset://document"), "restore source");
  auto cancelled = manager.request_reload<json_asset>(guid(1));
  require(cancelled.cancel(), "cancel reload");
  wait([&] { return !manager.database().has_active_operations() && loader.empty(); });
  require(manager.find<json_asset>(guid(1)) == second.try_get().get(),
          "cancel keeps cached resource");
  auto closing = manager.request_reload<json_asset>(guid(1));
  // 不 pump，让请求停留在等待 I/O 回执的阶段，再排空工作 scope。
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (loader.empty()) {
    require(std::chrono::steady_clock::now() < deadline, "shutdown setup timeout");
    std::this_thread::sleep_for(1ms);
  }
  worker.close();
  loader.abandon();
  (void)completions.drain();
  require(closing.done() && !closing.loaded() && !manager.database().has_active_operations(),
          "shutdown settles active request");
  require(static_cast<bool>(worker.get_executor().post(caors::tasks::task{[] {}})),
          "closed executor rejects work");
  require(loop.stop() == result::success && loop.failed_task_count() == 0, "drain real I/O");
  std::puts("CAORS + Gneiss scheduler + libuv: JSON load, sharing, cache, cancellation, failure, "
            "shutdown OK");
  return 0;
}
} // namespace

int main(int argc, char** argv) {
  try {
    return argc == 2 ? run(argv[1]) : 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
