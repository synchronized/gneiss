// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>

#include <cstdlib>
#include <exception>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
constexpr std::string_view scene_uuid = "12345678-1234-4234-8234-123456789abc";

gneiss::result create_application(gneiss::application& output) {
  const gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  return gneiss::application::create_native(desc, output);
}

template <typename Owner, auto Create, auto Destroy> bool verify_standalone() {
  static_assert(!std::is_copy_constructible_v<Owner>);
  static_assert(std::is_nothrow_move_assignable_v<Owner>);
  Owner first;
  if (first.reset().failed() || Create(first).failed()) {
    return false;
  }
  const auto original = first.get();
  Owner second{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): 移动后包装必须为空。
  if (first.get() != 0U || second.get() != original) {
    return false;
  }
  Owner third;
  if (Create(third).failed()) {
    return false;
  }
  const auto replaced = third.get();
  third = std::move(second);
  // NOLINTNEXTLINE(bugprone-use-after-move): 检查源与被覆盖资源的所有权。
  if (second.get() != 0U || third.get() != original ||
      Destroy(replaced) != GNEISS_ERROR_INVALID_HANDLE || Create(third).failed() ||
      Destroy(original) != GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  auto transferred = third.release();
  if constexpr (std::is_same_v<Owner, gneiss::application> ||
                std::is_same_v<Owner, gneiss::type_registry>) {
    if (third.get() != 0U || transferred.get() == 0U || transferred.reset().failed())
      return false;
  } else {
    if (third.get() != 0U || transferred == 0U || Destroy(transferred) != GNEISS_SUCCESS)
      return false;
  }
  if (third.reset().failed() || Create(third).failed())
    return false;
  const auto externally_destroyed = third.get();
  if (Destroy(externally_destroyed) != GNEISS_SUCCESS || third.reset().failed() ||
      third.get() != 0U || third.reset().failed()) {
    return false;
  }
  decltype(third.get()) scoped = [&] {
    Owner local;
    if (Create(local).failed()) {
      return decltype(third.get()){};
    }
    return local.get();
  }();
  return scoped != 0U && Destroy(scoped) == GNEISS_ERROR_INVALID_HANDLE;
}

template <typename Owner, auto Create> bool verify_thread_failure() {
  Owner resource;
  if (Create(resource).failed()) {
    return false;
  }
  const auto original = resource.get();
  gneiss::result close_result;
  gneiss::result replace_result;
  std::thread worker([&] {
    close_result = resource.reset();
    // 候选资源属于工作线程，但现有 output 仍属于主线程；替换必须失败并回收候选。
    replace_result = Create(resource);
  });
  worker.join();
  return close_result == gneiss::result::invalid_state &&
         replace_result == gneiss::result::invalid_state && resource.get() == original &&
         resource.reset().ok() && resource.get() == 0U;
}

bool verify_application_rollback() {
  std::uint64_t shutdown_count{};
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &shutdown_count;
  desc.shutdown = [](void* user_data) { ++*static_cast<std::uint64_t*>(user_data); };
  gneiss::application app;
  if (gneiss::application::create_native(desc, app).failed()) {
    return false;
  }
  const auto original = app.get();
  auto invalid = desc;
  invalid.struct_size = 0U;
  if (gneiss::application::create_native(invalid, app) != gneiss::result::invalid_argument ||
      app.get() != original || shutdown_count != 0U) {
    return false;
  }
  gneiss::result status;
  std::thread worker([&] { status = gneiss::application::create_native(desc, app); });
  worker.join();
  // 替换失败时新候选已执行 shutdown，原 Application 的所有权和回调上下文仍有效。
  return status == gneiss::result::invalid_state && app.get() == original && shutdown_count == 1U &&
         app.reset().ok() && shutdown_count == 2U;
}

bool verify_scene() {
  gneiss::application app;
  gneiss::scene_instance first;
  if (create_application(app).failed() ||
      gneiss::scene_instance::create_empty(app.get(), scene_uuid, first).failed()) {
    return false;
  }
  const auto original = first.get();
  if (gneiss::scene_instance::create_empty(app.get(), "invalid", first) !=
          gneiss::result::invalid_argument ||
      gneiss::scene_instance::load(app.get(), "", first) != gneiss::result::invalid_argument ||
      first.get() != original) {
    return false;
  }
  gneiss::result close_result;
  gneiss::result replace_result;
  std::thread worker([&] {
    close_result = first.reset();
    gneiss::application worker_app;
    replace_result = create_application(worker_app);
    if (replace_result.ok()) {
      replace_result = gneiss::scene_instance::create_empty(worker_app.get(), scene_uuid, first);
    }
  });
  worker.join();
  if (close_result != gneiss::result::invalid_state ||
      replace_result != gneiss::result::invalid_state || first.get() != original ||
      first.owner() != app.get()) {
    return false;
  }
  gneiss::scene_instance second{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): 移动同时清空父句柄和场景句柄。
  if (first.is_valid() || first.owner() != 0U || second.get() != original ||
      gneiss::scene_instance::create_empty(app.get(), scene_uuid, first).failed()) {
    return false;
  }
  const auto replaced = first.get();
  first = std::move(second);
  if (gneiss_scene_instance_unload(app.get(), replaced) != GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  const auto owner = first.owner();
  const auto released = first.release();
  return !first.is_valid() && first.owner() == 0U && released == original &&
         gneiss_scene_instance_unload(owner, released) == GNEISS_SUCCESS && first.reset().ok() &&
         gneiss::scene_instance::create_empty(app.get(), scene_uuid, first).ok() &&
         app.reset().ok() && first.reset().ok() && !first.is_valid();
}

void install_termination_handler() {
  // 在发生析构的线程安装处理器，避免依赖 CRT 的跨线程 handler 传播。
  std::set_terminate([] { std::_Exit(73); });
}

int verify_termination(std::string_view mode) {
  if (mode == "world") {
    gneiss::world resource;
    if (gneiss::world::create(resource).failed()) {
      return 1;
    }
    std::thread worker([resource = std::move(resource)]() mutable {
      install_termination_handler();
      gneiss::world local{std::move(resource)};
    });
    worker.join();
  } else if (mode == "application") {
    gneiss::application resource;
    if (create_application(resource).failed()) {
      return 1;
    }
    std::thread worker([resource = std::move(resource)]() mutable {
      install_termination_handler();
      gneiss::application local{std::move(resource)};
    });
    worker.join();
  } else if (mode == "scene") {
    gneiss::application app;
    gneiss::scene_instance resource;
    if (create_application(app).failed() ||
        gneiss::scene_instance::create_empty(app.get(), scene_uuid, resource).failed()) {
      return 1;
    }
    std::thread worker([resource = std::move(resource)]() mutable {
      install_termination_handler();
      gneiss::scene_instance local{std::move(resource)};
    });
    worker.join();
  }
  return 2;
}
} // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    return verify_termination(argv[1]);
  }
  if (!verify_standalone<gneiss::world, gneiss::world::create, gneiss_world_destroy>() ||
      !verify_standalone<gneiss::application, create_application, gneiss_application_destroy>() ||
      !verify_standalone<gneiss::type_registry, gneiss::type_registry::create,
                         gneiss_type_registry_destroy>()) {
    return 1;
  }
  if (!verify_thread_failure<gneiss::world, gneiss::world::create>() ||
      !verify_thread_failure<gneiss::application, create_application>() ||
      !verify_application_rollback() || !verify_scene()) {
    return 2;
  }
  // Registry 没有线程亲和约束；外部同步后可在其他线程销毁，不能误套 World 的规则。
  gneiss::type_registry registry;
  if (gneiss::type_registry::create(registry).failed()) {
    return 3;
  }
  gneiss::result status;
  std::thread worker([&] { status = registry.reset(); });
  worker.join();
  return status.ok() && !registry ? 0 : 4;
}
