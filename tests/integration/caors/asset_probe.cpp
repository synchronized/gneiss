// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

// 独立接入验证：不链接 Engine DLL，不将 CAORS 对象带入公共 SDK。
#include "engine/core/tasks/task_scheduler.hpp"
#include "file_system.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"
#include <caors/assets.h>
#include <caors/io.h>
#include <caors/tasks.h>
#include <yyjson.h>

#include <algorithm>
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

// CAORS 捕获来源并完成打开/读取；宿主推进后端与完成队列，worker 只负责解析。
// 当前仍是单资源验证，未切换生产资产路径。
class json_loader final : public caors::resources::async_loader<json_asset> {
  struct operation {
    caors::resources::async_load_context context;
    caors::resources::load_completion<json_asset> completion;
    caors::io::io_request<std::shared_ptr<caors::io::async_file>> opening;
    std::shared_ptr<caors::io::async_file> file;
    caors::io::io_request<caors::io::byte_buffer> reading;
  };

public:
  explicit json_loader(gneiss::caors_probe::file_system& files) : files_(files) {}
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
    auto request =
        caors::io::request_open(std::move(resolved.value), io_completions_.get_executor());
    if (!request) {
      (void)op->completion.complete({translate(request.error()), {}});
      return {};
    }
    op->opening = std::move(request.value);
    const std::scoped_lock lock(mutex_);
    pending_.push_back(std::move(op));
    ++starts;
    return {};
  }
  void pump() {
    files_.pump();
    (void)io_completions_.drain();
    const std::scoped_lock lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      const auto& op = *it;
      if (op->context.stop.stop_requested()) {
        (void)op->completion.complete({failure(errc::cancelled), {}});
        it = pending_.erase(it); // libuv 在途对象自行保活，宿主 loop 最后排空。
        continue;
      }
      result status = result::success;
      error io_error;
      if (op->opening) {
        auto opened = op->opening.result();
        if (!opened) {
          ++it;
          continue;
        }
        status = *opened ? result::success : result::io;
        if (!*opened)
          io_error = translate(opened->error());
        op->opening = {};
        if (status == result::success) {
          op->file = opened->value;
          auto request =
              caors::io::read_all_bounded(op->file, max_bytes, io_completions_.get_executor());
          if (!request) {
            status = result::io;
            io_error = translate(request.error());
          } else
            op->reading = std::move(request.value);
        }
      }
      std::shared_ptr<const caors::io::io_result<caors::io::byte_buffer>> read_result;
      std::span<const std::byte> bytes;
      if (status == result::success) {
        (void)io_completions_.drain();
        read_result = op->reading.result();
        status = !read_result ? result::not_ready : (*read_result ? result::success : result::io);
        if (read_result && !*read_result)
          io_error = translate(read_result->error());
        if (status == result::success)
          bytes = {read_result->value.data(), read_result->value.size()};
      }
      if (status == result::not_ready) {
        ++it;
        continue;
      }
      if (status != result::success) {
        (void)op->completion.complete({io_error, {}});
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
  void abandon() {
    files_.abandon();
    pending_.clear();
    (void)io_completions_.drain();
  }
  std::atomic_size_t starts{};
  static constexpr std::size_t max_bytes = 1024U * 1024U;

private:
  static error translate(caors::io::io_error error) {
    using caors::io::io_errc;
    if (error.value == io_errc::not_found)
      return failure(errc::not_found);
    if (error.value == io_errc::out_of_memory)
      return failure(errc::out_of_memory);
    if (error.value == io_errc::cancelled)
      return failure(errc::cancelled);
    if (error.value == io_errc::unsupported)
      return failure(errc::unsupported);
    if (error.value == io_errc::invalid_argument || error.value == io_errc::limit_exceeded ||
        error.value == io_errc::out_of_range)
      return failure(errc::invalid_argument);
    return failure(errc::backend_failure);
  }
  caors::tasks::execution_context io_completions_;
  gneiss::caors_probe::file_system& files_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<operation>> pending_;
};

void require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

void check_file_contract(gneiss::io_internal::uv_file_io& io, const char* path) {
  std::unique_ptr<gneiss::io_internal::uv_file_open_operation> opening;
  require(io.begin_open(path, opening) == result::success, "contract open");
  std::shared_ptr<gneiss::io_internal::uv_async_file> source;
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  result status;
  while ((status = opening->poll(source)) == result::not_ready) {
    require(std::chrono::steady_clock::now() < deadline, "contract open timeout");
    std::this_thread::sleep_for(1ms);
  }
  require(status == result::success && source->size() >= 8, "contract opened");
  auto file = std::make_shared<gneiss::caors_probe::async_file>(std::move(source));
  caors::tasks::execution_context queue;
  auto first = caors::io::request_read(file, 0, 4, 4, queue.get_executor());
  auto second = caors::io::request_read(file, 4, 4, 4, queue.get_executor());
  auto cancelled = caors::io::request_read(file, 0, 4, 4, queue.get_executor());
  require(first && second && cancelled, "ranges accepted");
  cancelled.value.cancel();
  auto invalid = caors::io::request_read(file, file->size(), 1, 4, queue.get_executor());
  require(!invalid, "invalid range rejected");
  auto over_budget = caors::io::request_read(file, 0, 4, 3, queue.get_executor());
  require(!over_budget, "budget enforced");
  auto closing = caors::io::request_close(file, queue.get_executor());
  auto closing_again = caors::io::request_close(file, queue.get_executor());
  require(closing && closing_again, "close subscriptions accepted");
  auto rejected = caors::io::request_read(file, 0, 1, 1, queue.get_executor());
  require(!rejected && rejected.error().value == caors::io::io_errc::closed,
          "read after close rejected");
  while (!closing.value.result() || !closing_again.value.result() || !first.value.result() ||
         !second.value.result() || !cancelled.value.result()) {
    file->pump();
    (void)queue.drain();
    require(std::chrono::steady_clock::now() < deadline, "contract timeout");
    std::this_thread::sleep_for(1ms);
  }
  require(static_cast<bool>(*closing.value.result()) &&
              static_cast<bool>(*closing_again.value.result()),
          "actual close completed");
  require(static_cast<bool>(*first.value.result()) && first.value.result()->value.size() == 4 &&
              static_cast<bool>(*second.value.result()) && second.value.result()->value.size() == 4,
          "accepted ranges finish despite close");
  require(cancelled.value.result()->error().value == caors::io::io_errc::cancelled,
          "cancelled range settles");
  require(io.begin_open(path, opening) == result::success, "abandon open");
  while ((status = opening->poll(source)) == result::not_ready) {
    require(std::chrono::steady_clock::now() < deadline, "abandon open timeout");
    std::this_thread::sleep_for(1ms);
  }
  require(status == result::success, "abandon opened");
  auto abandoned_file = std::make_shared<gneiss::caors_probe::async_file>(std::move(source));
  std::weak_ptr<gneiss::caors_probe::async_file> weak = abandoned_file;
  {
    auto abandoned_read = caors::io::request_read(abandoned_file, 0, 4, 4, queue.get_executor());
    require(static_cast<bool>(abandoned_read), "abandoned read accepted");
    abandoned_file->abandon();
    (void)queue.drain();
    require(abandoned_read.value.result() &&
                abandoned_read.value.result()->error().value == caors::io::io_errc::cancelled,
            "abandon settles receipt");
  }
  abandoned_file.reset();
  require(weak.expired(), "abandon releases ownership cycle");
}

void check_vfs_contract(gneiss::io_internal::uv_file_io& io, const char* path) {
  using namespace caors::io;
  std::weak_ptr<gneiss::caors_probe::file_system> lifetime;
  std::shared_ptr<caors::io::async_file> pinned;
  {
    auto files = std::make_shared<gneiss::caors_probe::file_system>(io);
    lifetime = files;
    caors::tasks::execution_context queue;
    struct cleanup {
      gneiss::caors_probe::file_system& files;
      caors::tasks::execution_context& queue;
      ~cleanup() {
        files.abandon();
        (void)queue.drain();
      }
    } guard{*files, queue};
    auto router = std::make_shared<file_system_resolver>();
    const auto original = std::filesystem::absolute(path).generic_string();
    const auto replacement = std::filesystem::absolute(__FILE__).generic_string();
    require(!router->mount("asset://test", files, original), "vfs mount");
    auto snapshot = route_snapshot::capture(router);
    require(static_cast<bool>(snapshot), "vfs snapshot");
    auto resolved = snapshot.value.resolve("asset://test");
    require(static_cast<bool>(resolved), "vfs resolve");
    const auto old_id = resolved.value.source_id;
    auto old = request_open(resolved.value, queue.get_executor());
    auto cancelled = request_open(resolved.value, queue.get_executor());
    require(old && cancelled, "vfs opens accepted");
    cancelled.value.cancel();
    require(router->unmount("asset://test"), "vfs unmount");
    require(!router->mount("asset://test", files, replacement), "vfs remount");
    auto current = router->resolve_owned("asset://test");
    require(current && current.value.source_id != old_id, "new mount identity");
    auto newer = request_open(current.value, queue.get_executor());
    auto stable = snapshot.value.resolve("asset://test");
    require(stable && stable.value.source_id == old_id &&
                stable.value.path.string_view() == resolved.value.path.string_view(),
            "stable snapshot");
    auto missing_location = router->resolve_owned("asset://test/missing");
    require(static_cast<bool>(missing_location), "missing resolve");
    auto missing = request_open(missing_location.value, queue.get_executor());
    require(newer && missing, "new opens accepted");
    router.reset(); // 真实打开尚未交付，销毁可变路由器。
    auto wait = [&](auto predicate) {
      const auto deadline = std::chrono::steady_clock::now() + 10s;
      while (!predicate()) {
        files->pump();
        (void)queue.drain();
        require(std::chrono::steady_clock::now() < deadline, "vfs timeout");
        std::this_thread::sleep_for(1ms);
      }
    };
    wait([&] {
      return old.value.result() && newer.value.result() && cancelled.value.result() &&
             missing.value.result();
    });
    require(static_cast<bool>(*old.value.result()) && static_cast<bool>(*newer.value.result()),
            "opens survive router destruction");
    require(cancelled.value.result()->error().value == io_errc::cancelled &&
                !*missing.value.result(),
            "cancel and missing settle");
    pinned = old.value.result()->value;
    require(pinned->source_id() == old_id &&
                newer.value.result()->value->source_id() == current.value.source_id,
            "file identity");
    auto old_read = request_read(pinned, 0, 8, 8, queue.get_executor());
    auto new_read = request_read(newer.value.result()->value, 0, 8, 8, queue.get_executor());
    require(old_read && new_read, "vfs reads accepted");
    wait([&] { return old_read.value.result() && new_read.value.result(); });
    auto expected_old = files->read_all(original, nullptr);
    auto expected_new = files->read_all(replacement, nullptr);
    require(expected_old && expected_new && *old_read.value.result() && *new_read.value.result(),
            "vfs read success");
    require(std::equal(old_read.value.result()->value.begin(), old_read.value.result()->value.end(),
                       expected_old.value.begin()) &&
                std::equal(new_read.value.result()->value.begin(),
                           new_read.value.result()->value.end(), expected_new.value.begin()),
            "old and new paths return correct bytes");
    files->abandon();
    auto refused = request_open(stable.value, queue.get_executor());
    require(!refused && refused.error().value == io_errc::closed, "stopped backend rejects open");
  }
  require(!lifetime.expired(), "returned file retains backend");
  pinned.reset();
  require(lifetime.expired(), "backend released with last file");
}

int run(const char* asset_path) {
  gneiss::io_internal::uv_loop_executor loop;
  require(loop.start() == result::success, "loop start");
  gneiss::io_internal::uv_file_io io(loop);
  check_file_contract(io, asset_path);
  check_vfs_contract(io, asset_path);
  gneiss::tasks::task_scheduler scheduler({.workers = 1});
  auto context = std::make_shared<worker_context>(scheduler);
  caors::tasks::external_executor worker(context);
  caors::tasks::execution_context completions; // 主线程队列，无新线程。
  caors::resources::cache<json_asset> cache;
  auto files = std::make_shared<gneiss::caors_probe::file_system>(io);
  json_loader loader(*files);
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
