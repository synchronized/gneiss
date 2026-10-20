// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/caors_vfs_bridge.hpp"
#include "engine/asset/native_file_system.hpp"
#include "engine/asset/source_revision_file_system.hpp"
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
void run(const char* root) {
  using namespace gneiss::asset_internal;
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
  auto native = std::make_shared<gneiss::asset_internal::native_file_system>();
  check(native->initialize(fixture.generic_string()) == GNEISS_SUCCESS, "root");
  native->set_async_io(std::make_shared<gneiss::io_internal::uv_file_io>(loop),
                       std::make_shared<gneiss::io_internal::uv_path_resolver>(loop));
  virtual_file_system source;
  check(source.mount("asset://", native) == GNEISS_SUCCESS, "mount");
  auto revisions = std::make_shared<source_revision_file_system>(source);
  virtual_file_system tracked;
  check(tracked.mount("asset://", revisions) == GNEISS_SUCCESS, "tracked mount");
  auto bridge = make_caors_vfs_bridge(tracked);
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
  auto cancelled = request_open(resolved.value, queue.get_executor());
  check(static_cast<bool>(cancelled), "cancelled open accepted");
  cancelled.value.cancel();
  auto opened = request_open(resolved.value, queue.get_executor());
  check(static_cast<bool>(opened), "open accepted");
  auto wait = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
      bridge->pump();
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
    run(argv[1]);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
