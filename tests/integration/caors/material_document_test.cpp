// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/caors_material_document.hpp"
#include "engine/asset/caors_vfs_bridge.hpp"
#include "engine/asset/native_file_system.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"
#include "uv_path_resolver.hpp"
#include <atomic>
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
void run() {
  using namespace gneiss::asset_internal;
  using namespace std::chrono_literals;
  using gneiss::result;
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-material-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "material.json";
  struct fixture_cleanup {
    std::filesystem::path path, root;
    ~fixture_cleanup() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
      std::filesystem::remove(root, ec);
    }
  } fixture{path, root};
  auto write = [&](std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    check(out.good(), "write fixture");
  };
  write(R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})");
  gneiss::io_internal::uv_loop_executor loop;
  check(loop.start() == result::success, "loop");
  auto native = std::make_shared<native_file_system>();
  check(native->initialize(root.generic_string()) == GNEISS_SUCCESS, "native root");
  native->set_async_io(std::make_shared<gneiss::io_internal::uv_file_io>(loop),
                       std::make_shared<gneiss::io_internal::uv_path_resolver>(loop));
  virtual_file_system files;
  check(files.mount("asset://", native) == GNEISS_SUCCESS, "mount");
  gneiss::tasks::task_scheduler scheduler({.workers = 1});
  auto context = gneiss::caors_internal::make_worker(scheduler);
  caors::tasks::external_executor worker(context);
  caors::tasks::execution_context completions;
  auto bridge = make_caors_vfs_bridge(files, worker.get_executor());
  std::atomic_bool reject_verification{false};
  const auto host_thread = std::this_thread::get_id();
  auto loader = make_material_document_loader(
      *bridge, [files, host_thread, &reject_verification](auto uri, auto bytes) {
        if (reject_verification.load())
          return result::invalid_state;
        if (std::this_thread::get_id() == host_thread)
          return result::internal;
        std::vector<std::byte> current;
        const result read{files.read_bounded(uri, 1024U * 1024U, current)};
        if (read != result::success)
          return read;
        return gneiss::core::sha256(current) == gneiss::core::sha256(bytes) ? result::success
                                                                            : result::invalid_state;
      });
  caors::resources::cache<material_document> cache;
  caors::assets::asset_manager manager({worker.get_executor(), completions.get_executor()});
  struct cleanup {
    caors::tasks::external_executor& worker;
    material_document_loader& loader;
    caors::tasks::execution_context& queue;
    ~cleanup() {
      worker.close();
      loader.abandon();
      (void)queue.drain();
    }
  } guard{worker, *loader, completions};
  check(!manager.register_type(cache, *loader, caors::string_name{"gneiss.material.document"}),
        "register");
  caors::guid::byte_array id_bytes{};
  id_bytes[0] = 1;
  const caors::resource_guid id{caors::guid::from_bytes(id_bytes)};
  caors::resources::file_source source(bridge);
  check(!manager.provider().bind(id, source, "asset://material.json"), "bind");
  auto wait = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
      loader->pump();
      (void)completions.drain();
      context->collect();
      check(std::chrono::steady_clock::now() < deadline, "timeout");
      std::this_thread::sleep_for(1ms);
    }
  };
  auto first = manager.request_load<material_document>(id);
  auto second = manager.request_load<material_document>(id);
  wait([&] { return first.done() && second.done(); });
  check(first.loaded() && second.loaded(), "loaded");
  auto old = first.try_get();
  check(old.get() == second.try_get().get() && loader->started_count() == 1, "coalesced");
  check(old->source.parameters.color[0] == 1, "red parsed");
  write(R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})");
  auto reload = manager.request_reload<material_document>(id);
  wait([&] { return reload.done(); });
  check(reload.loaded(), "reload");
  auto current = reload.try_get();
  check(current.get() != old.get() && current->digest != old->digest, "distinct version");
  check(old->source.parameters.color[0] == 1 && current->source.parameters.color[1] == 1,
        "old version retained");
  reject_verification = true;
  auto rejected = manager.request_reload<material_document>(id);
  wait([&] { return rejected.done(); });
  check(rejected.failed() && cache.find_handle(id).get() == current.get(),
        "verification failure preserves current");
  reject_verification = false;
  write("{}");
  auto invalid = manager.request_reload<material_document>(id);
  wait([&] { return invalid.done(); });
  check(invalid.failed() && cache.find_handle(id).get() == current.get(),
        "failure preserves current");
  auto cancelled = manager.request_reload<material_document>(id);
  check(cancelled.cancel(), "cancel");
  wait([&] { return !manager.database().has_active_operations() && loader->empty(); });
  check(cache.find_handle(id).get() == current.get(), "cancel preserves current");
  write(std::string(1024U * 1024U + 1U, ' '));
  auto oversized = manager.request_reload<material_document>(id);
  wait([&] { return oversized.done(); });
  check(oversized.failed() && cache.find_handle(id).get() == current.get(),
        "limit preserves current");
  check(cache.erase(id) && old->source.parameters.color[0] == 1 &&
            current->source.parameters.color[1] == 1,
        "handles survive eviction");
}
}
int main() {
  try {
    run();
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
