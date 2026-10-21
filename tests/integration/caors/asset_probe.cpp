// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

// 独立接入验证：不链接 Engine DLL，不将 CAORS 对象带入公共 SDK。
#include "engine/resource/caors_action_map.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/application/action_map_application.hpp"
#include "engine/resource/caors_action_map_request.hpp"
#include "engine/function/input/input_service.hpp"
#include "engine/platform/caors/adapter.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"
#include <caors/assets.h>
#include <caors/io.h>
#include <caors/tasks.h>

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
using gneiss::asset_internal::action_map_asset;
using gneiss::asset_internal::action_map_loader;

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
  auto file = gneiss::caors_internal::make_file(std::move(source));
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
  auto abandoned_file = gneiss::caors_internal::make_file(std::move(source));
  std::weak_ptr<gneiss::caors_internal::file_adapter> weak = abandoned_file;
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
  std::weak_ptr<gneiss::caors_internal::file_system_adapter> lifetime;
  std::shared_ptr<caors::io::async_file> pinned;
  {
    auto files = gneiss::caors_internal::make_file_system(io);
    lifetime = files;
    caors::tasks::execution_context queue;
    struct cleanup {
      gneiss::caors_internal::file_system_adapter& files;
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
  auto context = gneiss::caors_internal::make_worker(scheduler);
  caors::tasks::external_executor worker(context);
  caors::tasks::execution_context completions; // 主线程队列，无新线程。
  caors::resources::cache<action_map_asset> cache;
  auto files = gneiss::caors_internal::make_file_system(io);
  auto loader_owner = gneiss::asset_internal::make_action_map_loader(*files);
  auto& loader = *loader_owner;
  caors::assets::asset_manager manager({worker.get_executor(), completions.get_executor()});
  struct cleanup {
    caors::tasks::external_executor& worker;
    action_map_loader& loader;
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
  require(!manager.register_type(cache, loader, caors::string_name{"gneiss.input.action_map"}),
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
  auto first = manager.request_load<action_map_asset>(guid(1));
  auto second = manager.request_load<action_map_asset>(guid(1));
  require(first.cancel(), "cancel subscription");
  wait([&] { return second.done(); });
  require(first.cancelled() && second.loaded(), "independent cancellation");
  require(loader.started_count() == 1, "deduplicate loading");
  require(!second.try_get()->map.actions.empty(), "parsed typed action map");
  const auto& map = second.try_get()->map;
  require(map.actions.size() == 2 && map.actions[0].name == "move_horizontal" &&
              map.actions[0].bindings.size() == 2 && map.actions[0].bindings[0].physical_key == 4 &&
              map.actions[0].bindings[0].scale == -1.0F && map.actions[1].name == "quit",
          "typed values preserved");
  auto cached = manager.request_load<action_map_asset>(guid(1));
  require(cached.loaded() && cached.try_get().get() == second.try_get().get() &&
              loader.started_count() == 1,
          "cache hit");
  require(!manager.provider().bind(guid(2), source, "asset://document/missing"), "missing bind");
  auto missing = manager.request_load<action_map_asset>(guid(2));
  wait([&] { return missing.done(); });
  require(missing.failed(), "missing fails");
  // 源码文件可读但不是 JSON，区分 I/O 成功与解析失败。
  require(!router->mount("asset://invalid", files,
                         std::filesystem::absolute(__FILE__).generic_string()),
          "invalid mount");
  require(!manager.provider().rebind(guid(1), source, "asset://invalid"), "invalid rebind");
  auto invalid = manager.request_reload<action_map_asset>(guid(1));
  wait([&] { return invalid.done(); });
  require(invalid.failed(), "invalid JSON fails");
  require(manager.find<action_map_asset>(guid(1)) == second.try_get().get(),
          "failed reload keeps cached resource");
  require(
      !router->mount("asset://invalid-schema", files,
                     (std::filesystem::absolute(__FILE__).parent_path() / "invalid.input-map.json")
                         .generic_string()),
      "invalid schema mount");
  require(!manager.provider().rebind(guid(1), source, "asset://invalid-schema"), "schema rebind");
  auto invalid_schema = manager.request_reload<action_map_asset>(guid(1));
  wait([&] { return invalid_schema.done(); });
  require(invalid_schema.failed() &&
              manager.find<action_map_asset>(guid(1)) == second.try_get().get(),
          "valid JSON with invalid binding rejected without replacing cache");
  require(!manager.provider().rebind(guid(1), source, "asset://document"), "restore source");
  auto cancelled = manager.request_reload<action_map_asset>(guid(1));
  require(cancelled.cancel(), "cancel reload");
  wait([&] { return !manager.database().has_active_operations() && loader.empty(); });
  require(manager.find<action_map_asset>(guid(1)) == second.try_get().get(),
          "cancel keeps cached resource");
  using namespace gneiss::input_internal;
  using namespace gneiss::asset_internal;
  using namespace gneiss::application_internal;
  input_service input;
  require(input.replace_action_map({{{"old", {{4, 1.0F}}}}}) == result::success,
          "initial input map");
  action_id old_action;
  require(input.find_action("old", old_action) == result::success, "initial action");
  {
    auto request =
        gneiss::engine::make_action_map_request(manager.request_load<action_map_asset>(guid(1)));
    action_map output;
    require(request->take(output) == result::success && !output.actions.empty(),
            "native request result");
    const auto count = output.actions.size();
    require(request->take(output) == result::invalid_state && output.actions.size() == count,
            "native request transfers only once");
    request->cancel();
    require(request->take(output) == result::invalid_state,
            "native request cancellation is terminal");
  }
  action_map_application application;
  require(application.begin(gneiss::engine::make_action_map_request(
              manager.request_load<action_map_asset>(guid(1)))) == result::success,
          "begin input apply");
  action_state state;
  require(input.get_action_state(old_action, state) == result::success,
          "begin does not mutate input");
  require(application.poll(input) == result::success &&
              application.state() == action_map_apply_state::applied,
          "cached map applied on host");
  require(input.get_action_state(old_action, state) == result::invalid_handle,
          "old action invalidated");
  action_id movement;
  require(input.find_action("move_horizontal", movement) == result::success,
          "loaded action available");
  input_event pressed{};
  pressed.type = event_type::key;
  pressed.data.key = {
      .physical_key = 4, .logical_key = 0, .modifiers = 0, .action = key_action::pressed};
  require(input.push(pressed) && input.get_action_state(movement, state) == result::success &&
              state.held && state.value == -1.0F,
          "loaded binding responds to input");
  require(application.poll(input) == result::success &&
              input.get_action_state(movement, state) == result::success,
          "poll does not apply twice");
  require(!manager.provider().bind(guid(3), source, "asset://invalid-schema"),
          "invalid apply bind");
  require(application.begin(gneiss::engine::make_action_map_request(
              manager.request_load<action_map_asset>(guid(3)))) == result::success,
          "begin invalid apply");
  wait([&] { return application.poll(input) != result::not_ready; });
  require(application.state() == action_map_apply_state::failed &&
              input.get_action_state(movement, state) == result::success && state.value == -1.0F,
          "failed load retains active mapping");
  require(application.begin(gneiss::engine::make_action_map_request(
              manager.request_reload<action_map_asset>(guid(1)))) == result::success,
          "begin cancelled apply");
  application.cancel();
  require(application.poll(input) == result::invalid_state &&
              application.state() == action_map_apply_state::cancelled &&
              input.get_action_state(movement, state) == result::success,
          "cancel retains active mapping");
  wait([&] { return !manager.database().has_active_operations() && loader.empty(); });
  require(!router->mount(
              "asset://alternate", files,
              (std::filesystem::absolute(__FILE__).parent_path() / "alternate.input-map.json")
                  .generic_string()),
          "alternate mount");
  require(!manager.provider().bind(guid(4), source, "asset://alternate"), "alternate bind");
  auto shared_earlier = manager.request_load<action_map_asset>(guid(4));
  require(application.begin(gneiss::engine::make_action_map_request(
              manager.request_load<action_map_asset>(guid(4)))) == result::success,
          "begin earlier apply");
  require(application.begin(gneiss::engine::make_action_map_request(
              manager.request_load<action_map_asset>(guid(1)))) == result::success,
          "replace earlier apply");
  require(application.poll(input) == result::success, "latest cached apply");
  require(input.find_action("move_horizontal", movement) == result::success, "latest movement");
  wait([&] { return shared_earlier.done(); });
  require(shared_earlier.loaded() && application.poll(input) == result::success &&
              input.get_action_state(movement, state) == result::success,
          "late completion cannot replace latest map");
  action_id alternate;
  require(input.find_action("alternate", alternate) == result::not_found,
          "superseded map not applied");
  require(!manager.provider().bind(guid(5), source, "asset://alternate"),
          "uncached alternate bind");
  require(application.begin(gneiss::engine::make_action_map_request(
              manager.request_load<action_map_asset>(guid(5)))) == result::success &&
              application.poll(input) == result::not_ready &&
              input.get_action_state(movement, state) == result::success,
          "pending asynchronous apply preserves current map");
  wait([&] { return application.poll(input) != result::not_ready; });
  require(application.state() == action_map_apply_state::applied &&
              input.find_action("alternate", alternate) == result::success,
          "asynchronous alternate apply");
  pressed.data.key.physical_key = 7;
  require(input.push(pressed) && input.get_action_state(alternate, state) == result::success &&
              state.value == 1.0F,
          "alternate binding responds");
  {
    action_map_application abandoned;
    require(abandoned.begin(gneiss::engine::make_action_map_request(
                manager.request_reload<action_map_asset>(guid(1)))) == result::success,
            "begin abandoned apply");
  }
  wait([&] { return !manager.database().has_active_operations() && loader.empty(); });
  require(input.get_action_state(alternate, state) == result::success,
          "destroyed pending slot cannot mutate input");
  auto closing = manager.request_reload<action_map_asset>(guid(1));
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
