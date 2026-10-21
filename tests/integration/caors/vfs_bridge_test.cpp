// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/input/input_service.hpp"
#include "engine/resource/backend/caors/caors_action_map.hpp"
#include "engine/resource/backend/caors/caors_action_map_request.hpp"
#include "engine/resource/backend/caors/caors_vfs_bridge.hpp"
#include "engine/resource/vfs/native_file_system.hpp"
#include "engine/resource/vfs/source_revision_file_system.hpp"
#include "runtime/loading/action_map_application.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"
#include "uv_path_resolver.hpp"
#include <caors/io.h>
#include <caors/tasks.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
void run(const char* root, bool scheduled) {
  using namespace gneiss::engine::resource;
  using namespace caors::io;
  using namespace std::chrono_literals;
  gneiss::io_internal::uv_loop_executor loop;
  check(loop.start() == gneiss::result::success, "loop");
  const auto fixture =
      std::filesystem::temp_directory_path() /
      ("gneiss-caors-bridge-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(fixture / "input");
  const auto document = fixture / "input/default.input-map.json";
  std::filesystem::copy_file(std::filesystem::path(root) / "input/default.input-map.json",
                             document);
  struct remove_fixture {
    std::filesystem::path path;
    ~remove_fixture() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } fixture_cleanup{fixture};
  auto native = std::make_shared<gneiss::engine::resource::native_file_system>();
  check(native->initialize(fixture.generic_string()) == GNEISS_SUCCESS, "root");
  if (!scheduled)
    native->set_async_io(std::make_shared<gneiss::io_internal::uv_file_io>(loop),
                         std::make_shared<gneiss::io_internal::uv_path_resolver>(loop));
  virtual_file_system source;
  check(source.mount("asset://", native) == GNEISS_SUCCESS, "mount");
  auto revisions = std::make_shared<source_revision_file_system>(source);
  virtual_file_system tracked;
  check(tracked.mount("asset://", revisions) == GNEISS_SUCCESS, "tracked mount");
  caors::tasks::execution_context work;
  auto bridge = make_caors_vfs_bridge(tracked, work.get_executor(), 1024U * 1024U, scheduled);
  caors::tasks::execution_context queue;
  struct cleanup {
    caors_vfs_bridge& bridge;
    caors::tasks::execution_context& queue;
    ~cleanup() {
      bridge.abandon();
      (void)queue.drain();
    }
  } guard{*bridge, queue};
  const char* uri = "asset://input/default.input-map.json";
  asset_identity expected, actual;
  check(source.identify(uri, 1, expected) == GNEISS_SUCCESS &&
            bridge->identify(uri, 1, actual) == GNEISS_SUCCESS && actual == expected,
        "original source identity");
  auto snapshot = route_snapshot::capture(bridge);
  check(static_cast<bool>(snapshot), "snapshot");
  auto resolved = snapshot.value.resolve(uri);
  check(static_cast<bool>(resolved), "resolve");
  if (scheduled) {
    auto discarded = request_open(resolved.value, queue.get_executor());
    check(discarded && work.pending() == 1, "scheduled open is queued");
    bridge->pump();
    (void)queue.drain();
    check(!discarded.value.result(), "pump does not execute synchronous I/O");
    work.clear();
    bridge->pump();
    (void)queue.drain();
    check(discarded.value.result() && !*discarded.value.result(), "discarded open settles");

    auto limited = make_caors_vfs_bridge(tracked, work.get_executor(), 1, true);
    auto route = route_snapshot::capture(limited);
    auto location = route.value.resolve(uri);
    auto request = request_open(location.value, queue.get_executor());
    check(static_cast<bool>(request), "limited open admitted");
    (void)work.drain();
    limited->pump();
    (void)queue.drain();
    check(request.value.result() && !*request.value.result(), "snapshot limit enforced");
    limited->abandon();
  }
  auto cancelled = request_open(resolved.value, queue.get_executor());
  check(static_cast<bool>(cancelled), "cancelled open accepted");
  cancelled.value.cancel();
  auto opened = request_open(resolved.value, queue.get_executor());
  check(static_cast<bool>(opened), "open accepted");
  auto wait = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
      bridge->pump();
      // 同一桥分别验证显式协作推进与后台队列。
      if (scheduled) {
        (void)work.drain();
      } else {
        std::thread worker([&] { (void)work.drain(); });
        worker.join();
      }
      (void)queue.drain();
      check(std::chrono::steady_clock::now() < deadline, "timeout");
      std::this_thread::sleep_for(1ms);
    }
  };
  wait([&] { return opened.value.result(); });
  wait([&] { return cancelled.value.result(); });
  check(cancelled.value.result()->error().value == io_errc::cancelled, "cancelled open settles");
  check(static_cast<bool>(*opened.value.result()), "opened");
  auto file = opened.value.result()->value;
  auto reading = request_read(file, 0, file->size(), 1024U * 1024U, queue.get_executor());
  check(static_cast<bool>(reading), "read accepted");
  auto partial = request_read(file, 1, 1, 1, queue.get_executor());
  check(!partial && partial.error().value == io_errc::unsupported, "no partial digest");
  auto closing = request_close(file, queue.get_executor());
  auto again = request_close(file, queue.get_executor());
  check(closing && again, "close accepted");
  auto late = request_read(file, 0, file->size(), 1024U * 1024U, queue.get_executor());
  check(!late && late.error().value == io_errc::closed, "closed rejects read");
  const auto queued_deadline = std::chrono::steady_clock::now() + 10s;
  while (work.pending() == 0) {
    bridge->pump();
    (void)queue.drain();
    check(std::chrono::steady_clock::now() < queued_deadline, "worker dispatch timeout");
    std::this_thread::sleep_for(1ms);
  }
  bridge->pump();
  (void)queue.drain();
  check(!reading.value.result() && !closing.value.result() && !again.value.result(),
        "host pump neither hashes nor closes ahead of worker");
  wait([&] { return reading.value.result() && closing.value.result() && again.value.result(); });
  check(*reading.value.result() && *closing.value.result() && *again.value.result(),
        "read and close complete");
  source_revision_file_system::source_revision revision;
  check(revisions->observed_revision("input/default.input-map.json", revision) == GNEISS_SUCCESS &&
            revision.bytes == file->size(),
        "digest recorded");
  check(revisions->verify({}) == GNEISS_SUCCESS, "source verified");
  for (const char* rejected :
       {"asset://../CMakeLists.txt", "asset://input/missing.json", "unknown://file"}) {
    auto location = snapshot.value.resolve(rejected);
    if (!location)
      continue;
    auto attempt = request_open(location.value, queue.get_executor());
    if (!attempt)
      continue;
    wait([&] { return attempt.value.result(); });
    check(!*attempt.value.result(), "unsafe or missing source rejected");
  }
  // 同一 VFS 桥交给真实输入 Loader，不另建 CAORS 路由表。
  {
    using namespace gneiss::engine::input;
    using namespace gneiss::runtime;
    caors::resources::cache<action_map_asset> cache;
    bool reject_revision = false;
    const auto host_thread = std::this_thread::get_id();
    auto loader = make_action_map_loader(*bridge, [&](auto, auto) {
      check(std::this_thread::get_id() != host_thread, "final verification runs on worker");
      return reject_revision ? gneiss::result::invalid_state : gneiss::result::success;
    });
    caors::tasks::execution_context completed;
    caors::assets::asset_manager manager({work.get_executor(), completed.get_executor()});
    caors::resources::file_source provider(bridge);
    const caors::resource_guid id{caors::guid::from_bytes(caors::guid::byte_array{1})};
    check(!manager.provider().bind(id, provider, uri), "input provider");
    check(!manager.register_type(cache, *loader, caors::string_name{"gneiss.input.action_map"}),
          "input loader");
    auto request = manager.request_load<action_map_asset>(id);
    const auto input_deadline = std::chrono::steady_clock::now() + 10s;
    while (!request.done()) {
      loader->pump();
      std::thread worker([&] { (void)work.drain(); });
      worker.join();
      (void)completed.drain();
      check(std::chrono::steady_clock::now() < input_deadline, "input timeout");
      std::this_thread::sleep_for(1ms);
    }
    check(request.loaded(), "typed input loaded through bridge");
    check(revisions->verify({}) == GNEISS_SUCCESS, "verify before input application");
    const auto previous_asset = request.try_get();
    reject_revision = true;
    auto rejected_reload = manager.request_reload<action_map_asset>(id);
    while (!rejected_reload.done()) {
      loader->pump();
      std::thread worker([&] { (void)work.drain(); });
      worker.join();
      (void)completed.drain();
      check(std::chrono::steady_clock::now() < input_deadline, "verification rejection timeout");
      std::this_thread::sleep_for(1ms);
    }
    check(rejected_reload.failed() && cache.find(id) == previous_asset.get(),
          "verification failure preserves cached version");
    input_service input;
    action_map_application application;
    check(application.begin(gneiss::engine::resource::make_action_map_request(
              manager.request_load<action_map_asset>(id))) == gneiss::result::success &&
              application.poll(input) == gneiss::result::success,
          "apply verified input");
    action_id action;
    check(input.find_action("move_horizontal", action) == gneiss::result::success,
          "real input action available");
  }
  // 已接受的工作被宿主丢弃，也必须释放关闭计数和读取凭证。
  auto dropped_open = request_open(resolved.value, queue.get_executor());
  check(static_cast<bool>(dropped_open), "drop open");
  wait([&] { return dropped_open.value.result(); });
  check(static_cast<bool>(*dropped_open.value.result()), "drop opened");
  auto dropped_file = dropped_open.value.result()->value;
  auto dropped_read =
      request_read(dropped_file, 0, dropped_file->size(), 1024U * 1024U, queue.get_executor());
  auto dropped_close = request_close(dropped_file, queue.get_executor());
  check(dropped_read && dropped_close, "drop requests");
  const auto drop_deadline = std::chrono::steady_clock::now() + 10s;
  while (work.pending() == 0) {
    bridge->pump();
    (void)queue.drain();
    check(std::chrono::steady_clock::now() < drop_deadline, "drop queue timeout");
    std::this_thread::sleep_for(1ms);
  }
  work.clear();
  wait([&] { return dropped_read.value.result() && dropped_close.value.result(); });
  check(dropped_read.value.result()->error().value == io_errc::cancelled &&
            static_cast<bool>(*dropped_close.value.result()),
        "dropped worker releases close barrier");
  {
    std::ofstream changed(document, std::ios::binary | std::ios::app);
    changed << " ";
    check(static_cast<bool>(changed), "modify fixture");
  }
  check(revisions->verify({}) == GNEISS_ERROR_INVALID_STATE, "changed source rejected");
  bridge->abandon();
  auto stopped = request_open(resolved.value, queue.get_executor());
  check(!stopped && stopped.error().value == io_errc::closed, "abandoned bridge");
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2)
      return 2;
    run(argv[1], false);
    run(argv[1], true);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
