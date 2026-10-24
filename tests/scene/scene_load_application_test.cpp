// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/c_owner.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/loaders/mesh_binary.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"
#include "runtime/application.hpp"
#include "runtime/application_asset_reload_internal.hpp"
#include "runtime/application_registry.hpp"
#include "runtime/application_scene_load_internal.hpp"

#include "engine/values/input.hpp"
#include "engine/values/log.hpp"
#include "engine/values/render.hpp"
#include "engine/values/scene.hpp"
#include <gneiss/engine/application.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace runtime;
void check(bool condition, std::source_location where = std::source_location::current()) {
  if (!condition) {
    throw std::runtime_error("场景 Application 契约失败，行=" + std::to_string(where.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-scene-load-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    gneiss::engine::mesh_binary_data mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0}, {0, 0, 1}},
                     {{1, 0, 0}, {1, 0}, {0, 0, 1}},
                     {{0, 1, 0}, {0, 1}, {0, 0, 1}}};
    mesh.indices = {0, 1, 2};
    mesh.tangents.assign(3, {1, 0, 0, 1});
    mesh.uv1.assign(3, {0.5F, 0.5F});
    mesh.colors.assign(3, {1, 1, 1, 1});
    std::vector<std::byte> bytes;
    gneiss::engine::mesh_binary_diagnostic diagnostic;
    check(gneiss::engine::encode_mesh_binary(mesh, bytes, diagnostic) ==
          gneiss::engine::mesh_binary_result::success);
    std::ofstream output(root / "mesh.json", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::ofstream(root / "material.json")
        << R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}}],"prefab_instances":[]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void drain_visuals(gneiss_application handle, gneiss::engine::task_scheduler& scheduler,
                   gneiss::engine::execution_mode mode) {
  auto app = find_application(handle);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    app->advance_scene_load();
    std::this_thread::yield();
  } while (app->visual_assets_loading());
}

void visual_failure(gneiss::engine::execution_mode mode) {
  fixture files;
  std::ofstream(files.root / "material.json") << "broken";
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application_configuration config;
  const auto root = files.root.string();
  config.asset_root = root;
  unsigned failures{};
  config.callbacks.diagnostic = [&](auto, const auto& event) {
    if (event.module == "scene.assets") {
      ++failures;
    }
  };
  std::shared_ptr<application> app;
  check(application::create(config, app) == gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  std::uint64_t request{};
  check(app->request_scene("asset://scene.json", 1U, 1U, request) ==
        gneiss::engine::result::success);
  const auto step = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    app->advance_scene_load();
    std::this_thread::yield();
  };
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      step();
    }
  };
  scene_load_progress progress;
  until([&] { return app->scene_progress(progress) && progress.phase == scene_load_phase::ready; });
  check(progress.upload_reserved_bytes == 0U && !progress.gpu_in_flight);
  scene_load_completion completion;
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  scene_node_id node;
  check(app->scenes()->find_node(completion.scene, "00000000-0000-4000-8000-000000000002", node) ==
        gneiss::engine::result::success);
  gneiss::engine::scene_mesh_binding binding;
  check(app->capture_mesh_binding(completion.scene, node, binding) ==
        gneiss::engine::result::success);
  check(binding.mesh && binding.material);
  auto material = binding.material;
  until([&] { return !app->visual_assets_loading(); });
  check(failures == 1U);
  gneiss::engine::render_asset_ref ready;
  check(app->query_ready_asset({"asset://mesh.json", gneiss::engine::render_asset_type::mesh},
                               ready) == gneiss::engine::result::success);
  check(ready == binding.mesh);
  check(
      app->query_ready_asset({"asset://material.json", gneiss::engine::render_asset_type::material},
                             ready) == gneiss::engine::result::not_ready);
  std::uint64_t nodes{};
  check(app->scenes()->get_node_count(completion.scene, &nodes) ==
            gneiss::engine::result::success &&
        nodes == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  const std::array sources{gneiss::engine::render_asset_reload{
      "asset://material.json", gneiss::engine::render_asset_type::material}};
  check(app->request_assets(sources, 1U, 2U, request, true) == gneiss::engine::result::success);
  gneiss::engine::texture_load_completion loaded;
  until([&] {
    bool finished{};
    check(app->poll_assets(loaded, finished) == gneiss::engine::result::success);
    return finished;
  });
  check(loaded.result == gneiss::engine::result::success && loaded.assets.front() == material);
  check(app->capture_mesh_binding(completion.scene, node, binding) ==
            gneiss::engine::result::success &&
        binding.material == material);
  // 已激活场景继续持有稳定资源，关闭会等待所属任务并释放其作用域。
  binding = {};
  material = {};
  loaded = {};
  ready = {};
  app.reset();
  check(scheduler.stats().retained == 0U);
}

void run(gneiss::engine::execution_mode mode) {
  fixture files;
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::engine::result{gneiss_application_create(&desc, app.put())} ==
        gneiss::engine::result::success);
  // 未装配执行器时，宿主入口应保留空闲查询与未就绪操作的不同语义。
  std::uint64_t unattached_request = 99U;
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 1U, unattached_request) ==
            gneiss::engine::result::not_ready &&
        unattached_request == 0U);
  scene_load_progress idle_progress;
  bool idle_active = true;
  check(query_scene_load_progress(app.get(), idle_progress, idle_active) ==
            gneiss::engine::result::success &&
        !idle_active);
  scene_load_completion idle_completion;
  bool idle_ready = true;
  check(poll_scene_load(app.get(), idle_completion, idle_ready) ==
            gneiss::engine::result::not_ready &&
        !idle_ready);
  check(cancel_scene_load(app.get(), 99U) == gneiss::engine::result::not_ready);
  check(attach_task_executor(app.get(), scheduler) == gneiss::engine::result::success);
  gneiss_world old_world{};
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &old_world)} ==
        gneiss::engine::result::success);
  gneiss_entity_id old_entity{};
  check(gneiss::engine::result{gneiss_world_entity_create(old_world, &old_entity)} ==
        gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://scene.json";
  std::uint64_t request{};
  check(request_scene_load(app.get(), uri, 1U, 1U, request) == gneiss::engine::result::success);
  const auto first = request;
  std::uint64_t rejected{};
  check(request_scene_load(app.get(), uri, 1U, 2U, rejected) == gneiss::engine::result::not_ready &&
        rejected == 0U);
  auto drive = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    scene_load_completion completion;
    bool ready{};
    check(poll_scene_load(app.get(), completion, ready) == gneiss::engine::result::success);
    std::this_thread::yield();
    return std::pair{ready, std::move(completion)};
  };
  auto await_phase = [&](scene_load_phase phase) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      auto [finished, completion] = drive();
      check(!finished);
      scene_load_progress progress;
      bool active{};
      check(query_scene_load_progress(app.get(), progress, active) ==
                gneiss::engine::result::success &&
            active);
      if (progress.phase == phase) {
        return progress;
      }
    }
    throw std::runtime_error("等待场景阶段超时");
  };
  const auto prepared = await_phase(scene_load_phase::ready);
  // 结构 ready 时视觉内容尚未读取或上传。
  check(prepared.resident_bytes == 0U && !prepared.gpu_in_flight);
  gneiss::engine::render_asset_ref not_ready;
  check(query_ready_asset(app.get(), {"asset://mesh.json", gneiss::engine::render_asset_type::mesh},
                          not_ready) == gneiss::engine::result::not_ready);
  check(prepared.cpu_data_bytes == prepared.resident_bytes);
  check(prepared.application_logical_bytes >= prepared.resident_bytes);
  check(prepared.application_cpu_data_bytes >= prepared.cpu_data_bytes);
  check(prepared.available_bytes +
            std::max(prepared.application_logical_bytes, prepared.application_cpu_data_bytes) ==
        gneiss::engine::render_resource_service::default_memory_limit);
  check(prepared.upload_reserved_bytes == 0U);
  gneiss_world observed{};
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &observed)} ==
            gneiss::engine::result::success &&
        observed == old_world);
  std::uint64_t count{};
  check(gneiss::engine::result{gneiss_world_entity_count(old_world, &count)} ==
            gneiss::engine::result::success &&
        count == 1U);
  scene_load_completion completion;
  check(activate_scene_load(app.get(), first + 100U, completion) ==
        gneiss::engine::result::not_ready);
  check(activate_scene_load(app.get(), first, completion) == gneiss::engine::result::success);
  check(completion.scene != GNEISS_NULL_SCENE_INSTANCE &&
        completion.progress.phase == scene_load_phase::applied);
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &observed)} ==
            gneiss::engine::result::success &&
        observed != old_world);
  check(cancel_scene_load(app.get(), first) == gneiss::engine::result::not_ready);
  // 激活后的旧域由 session 延迟退役，期间不能接收下一次打开。
  check(gneiss::engine::result{gneiss_world_entity_count(old_world, &count)} ==
            gneiss::engine::result::success &&
        count == 1U);
  check(request_scene_load(app.get(), uri, 1U, 2U, rejected) == gneiss::engine::result::not_ready &&
        rejected == 0U);
  (void)drive();
  check(gneiss::engine::result{gneiss_world_entity_count(old_world, &count)} ==
        gneiss::engine::result::invalid_handle);

  // ready 阶段仍可取消，不改写活动域；迟到请求身份不能取消后续请求。
  check(request_scene_load(app.get(), uri, 1U, 2U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(cancel_scene_load(app.get(), first) == gneiss::engine::result::not_ready);
  check(cancel_scene_load(app.get(), request) == gneiss::engine::result::success);
  auto [finished, cancelled] = drive();
  check(finished && cancelled.progress.phase == scene_load_phase::cancelled);
  check(cancelled.progress.cleanup_complete && !cancelled.progress.gpu_in_flight);
  check(cancelled.progress.upload_reserved_bytes == 0U);
  check(cancelled.progress.application_logical_bytes <=
        gneiss::engine::render_resource_service::default_memory_limit);
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &old_world)} ==
            gneiss::engine::result::success &&
        old_world == observed);

  // 描述已准备后改变源文件：最终复读必须拒绝发布，旧 World 保持存活。
  check(request_scene_load(app.get(), uri, 1U, 3U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::assets);
  std::ofstream(files.root / "scene.json", std::ios::app) << ' ';
  const auto changed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool changed{};
  while (!changed && std::chrono::steady_clock::now() < changed_deadline) {
    auto result = drive();
    if (result.first) {
      check(result.second.progress.phase == scene_load_phase::failed &&
            result.second.result == gneiss::engine::result::invalid_state);
      check(result.second.progress.cleanup_complete &&
            result.second.progress.upload_reserved_bytes == 0U);
      changed = true;
    }
  }
  check(changed &&
        gneiss::engine::result{gneiss_world_entity_count(observed, &count)} ==
            gneiss::engine::result::success &&
        count == 1U);

  // 视觉来源损坏不阻止结构 ready；取消仍保持旧 World。
  std::ofstream(files.root / "material.json") << "broken";
  check(request_scene_load(app.get(), uri, 1U, 3U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(cancel_scene_load(app.get(), request) == gneiss::engine::result::success);
  check(drive().first);
  check(gneiss::engine::result{gneiss_world_entity_count(observed, &count)} ==
            gneiss::engine::result::success &&
        count == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  check(request_scene_load(app.get(), uri, 1U, 4U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(activate_scene_load(app.get(), request, completion) == gneiss::engine::result::success);
  (void)drive();
  // ready 只完成结构，取消仍必须保留已经激活的 World。
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &observed)} ==
        gneiss::engine::result::success);
  for (const auto phase :
       {scene_load_phase::preparing, scene_load_phase::instantiating, scene_load_phase::ready}) {
    check(request_scene_load(app.get(), uri, 1U, 5U, request) == gneiss::engine::result::success);
    if (phase != scene_load_phase::preparing) {
      (void)await_phase(phase);
    }
    check(cancel_scene_load(app.get(), request) == gneiss::engine::result::success);
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool stopped{};
    while (!stopped && std::chrono::steady_clock::now() < cancel_deadline) {
      auto [done, cancelled_result] = drive();
      if (done) {
        check(cancelled_result.progress.phase == scene_load_phase::cancelled);
        check(cancelled_result.progress.cleanup_complete &&
              cancelled_result.progress.upload_reserved_bytes == 0U);
        stopped = true;
      }
    }
    check(stopped &&
          gneiss::engine::result{gneiss_world_entity_count(observed, &count)} ==
              gneiss::engine::result::success &&
          count == 1U);
  }
  // 无渲染依赖的空场景不需要采用批次，也必须正常激活。
  std::ofstream(files.root / "empty.scene")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000003","objects":[],"prefab_instances":[]})";
  check(request_scene_load(app.get(), "asset://empty.scene", 1U, 6U, request) ==
        gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(activate_scene_load(app.get(), request, completion) == gneiss::engine::result::success);
  (void)drive();
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &observed)} ==
        gneiss::engine::result::success);
  check(gneiss::engine::result{gneiss_world_entity_count(observed, &count)} ==
            gneiss::engine::result::success &&
        count == 0U);
  check(request_scene_load(app.get(), uri, 1U, 7U, request) == gneiss::engine::result::success);
  app = gneiss::test_support::application_owner{};
  check(scheduler.stats().retained == 0U);
  check(gneiss::engine::result{gneiss_world_entity_count(observed, &count)} ==
        gneiss::engine::result::invalid_handle);
}

void shared_scene_assets(gneiss::engine::execution_mode mode) {
  fixture files;
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::engine::result{gneiss_application_create(&desc, app.put())} ==
        gneiss::engine::result::success);
  check(attach_task_executor(app.get(), scheduler) == gneiss::engine::result::success);
  scene_load_completion completion;
  std::uint64_t request{};
  auto poll = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(poll_scene_load(app.get(), completion, terminal) == gneiss::engine::result::success);
    std::this_thread::yield();
    return terminal;
  };
  auto prepare = [&] {
    check(request_scene_load(app.get(), "asset://scene.json", 1U, 1U, request) ==
          gneiss::engine::result::success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    scene_load_progress progress;
    do {
      check(std::chrono::steady_clock::now() < deadline && !poll());
      bool active{};
      check(query_scene_load_progress(app.get(), progress, active) ==
                gneiss::engine::result::success &&
            active);
    } while (progress.phase != scene_load_phase::ready);
    return progress;
  };
  auto selected = [&](gneiss::engine::render_asset_type type, const char* uri) {
    gneiss::engine::render_asset_ref lease;
    check(query_ready_asset(app.get(), {.uri = uri, .type = type}, lease) ==
          gneiss::engine::result::success);
    return lease;
  };
  auto binding = [&] {
    constexpr std::string_view uuid = "00000000-0000-4000-8000-000000000002";
    gneiss_scene_node_id node{};
    check(gneiss::engine::result{gneiss_scene_instance_find_node(
              app.get(), completion.scene, uuid.data(), uuid.size(), &node)} ==
          gneiss::engine::result::success);
    mesh_binding value;
    check(capture_mesh_binding(app.get(), completion.scene, node, value) ==
          gneiss::engine::result::success);
    return value;
  };
  (void)prepare();
  check(activate_scene_load(app.get(), request, completion) == gneiss::engine::result::success);
  drain_visuals(app.get(), scheduler, mode);
  auto original = binding();
  auto mesh = selected(gneiss::engine::render_asset_type::mesh, "asset://mesh.json");
  auto material = selected(gneiss::engine::render_asset_type::material, "asset://material.json");
  gneiss_world old_world{};
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &old_world)} ==
        gneiss::engine::result::success);
  check(!poll());
  // 两个 World 同时持有相同版本，不重复计量或创建后端资源。
  const auto second = prepare();
  check(second.application_logical_bytes > 0U);
  check(activate_scene_load(app.get(), request, completion) == gneiss::engine::result::success);
  auto reused = binding();
  check(reused.mesh == mesh && reused.material == material);
  gneiss_world new_world{};
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &new_world)} ==
            gneiss::engine::result::success &&
        old_world != new_world);
  std::uint64_t count{};
  check(gneiss::engine::result{gneiss_world_entity_count(old_world, &count)} ==
        gneiss::engine::result::success);
  check(!poll());
  check(gneiss::engine::result{gneiss_world_entity_count(old_world, &count)} ==
        gneiss::engine::result::invalid_handle);
  check(gneiss::engine::result{gneiss_world_entity_count(new_world, &count)} ==
            gneiss::engine::result::success &&
        count == 1U);
  // 更新完成后保持稳定资源引用；取消场景切换不撤回已更新的共享资源。
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  (void)prepare();
  check(selected(gneiss::engine::render_asset_type::material, "asset://material.json") == material);
  check(cancel_scene_load(app.get(), request) == gneiss::engine::result::success);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!poll()) {
    check(std::chrono::steady_clock::now() < deadline);
  }
  check(completion.progress.phase == scene_load_phase::cancelled &&
        completion.progress.cleanup_complete);
  check(selected(gneiss::engine::render_asset_type::material, "asset://material.json") == material);
  // 重试和激活都继续复用同一资源身份。
  (void)prepare();
  check(activate_scene_load(app.get(), request, completion) == gneiss::engine::result::success);
  auto changed = binding();
  check(changed.mesh == mesh && changed.material == material);
  check(selected(gneiss::engine::render_asset_type::material, "asset://material.json") ==
        changed.material);
  check(!poll());
  check(original.mesh == mesh && original.material == material);
  // 已缓存资源不因打开场景而隐式重读损坏文件；显式重载独立处理。
  check(gneiss::engine::result{gneiss_application_get_world(app.get(), &new_world)} ==
        gneiss::engine::result::success);
  std::ofstream(files.root / "material.json") << "broken";
  (void)prepare();
  check(activate_scene_load(app.get(), request, completion) == gneiss::engine::result::success);
  drain_visuals(app.get(), scheduler, mode);
  check(selected(gneiss::engine::render_asset_type::material, "asset://material.json") ==
        changed.material);
}

void shared_texture_versions(gneiss::engine::execution_mode mode) {
  fixture files;
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::engine::encode_texture_ktx2(
            {
                .transfer = gneiss::engine::texture_transfer::srgb,
                .levels =
                    {
                        {
                            .width = 1U,
                            .height = 1U,
                            .pixels = std::vector<std::byte>(4U, std::byte{255}),
                        },
                    },
            },
            bytes, diagnostic) == gneiss::engine::texture_ktx2_result::success);
  {
    std::ofstream output(files.root / "image.ktx2", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::ofstream(files.root / "texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  for (const auto* name : {"material.json", "other.json"}) {
    std::ofstream(files.root / name)
        << R"({"format":"gneiss.material","version":2,"color":[1,1,1,1],"base_color_texture":"asset://texture.json"})";
  }
  std::ofstream(files.root / "scene.json")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}},)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://other.json"}}}],"prefab_instances":[]})";
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::engine::result{gneiss_application_create(&desc, app.put())} ==
        gneiss::engine::result::success);
  check(attach_task_executor(app.get(), scheduler) == gneiss::engine::result::success);
  std::uint64_t request{};
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 1U, request) ==
        gneiss::engine::result::success);
  scene_load_completion scene_completion;
  scene_load_progress progress;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    bool active{};
    check(poll_scene_load(app.get(), scene_completion, terminal) ==
              gneiss::engine::result::success &&
          !terminal);
    check(query_scene_load_progress(app.get(), progress, active) ==
              gneiss::engine::result::success &&
          active);
    std::this_thread::yield();
  } while (progress.phase != scene_load_phase::ready);
  // 材质各自一批准备，共享纹理必须复用同一 RID，绑定检查及预算去重均应成功。
  check(progress.texture_payload_bytes == 0U);
  check(activate_scene_load(app.get(), request, scene_completion) ==
        gneiss::engine::result::success);
  drain_visuals(app.get(), scheduler, mode);
  std::uint64_t count{};
  check(gneiss::engine::result{gneiss_scene_instance_get_node_count(
            app.get(), scene_completion.scene, &count)} == gneiss::engine::result::success &&
        count == 2U);
  const std::array<std::string, 1> textures{"asset://texture.json"};
  check(request_textures(app.get(), textures, 1U, 2U, request) == gneiss::engine::result::success);
  gneiss::engine::texture_load_completion assets;
  bool ready{};
  while (!ready) {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(poll_textures(app.get(), assets, ready) == gneiss::engine::result::success);
    std::this_thread::yield();
  }
  // 显式纹理重载仅返回该纹理，不重读材质或返回历史版本。
  check(assets.result == gneiss::engine::result::success && assets.assets.size() == 1U &&
        assets.peak_upload_bytes > 0U);
  // 视觉文件变化不属于场景结构快照；打开场景复用当前成功资源。
  bool ignored{};
  check(poll_scene_load(app.get(), scene_completion, ignored) == gneiss::engine::result::success &&
        !ignored);
  std::ofstream(files.root / "image.ktx2", std::ios::app).put('\0');
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 3U, request) ==
        gneiss::engine::result::success);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool active{};
    check(poll_scene_load(app.get(), scene_completion, ignored) ==
              gneiss::engine::result::success &&
          !ignored);
    check(query_scene_load_progress(app.get(), progress, active) ==
              gneiss::engine::result::success &&
          active);
  } while (progress.phase != scene_load_phase::ready);
  check(activate_scene_load(app.get(), request, scene_completion) ==
        gneiss::engine::result::success);
  drain_visuals(app.get(), scheduler, mode);
}
} // namespace
int main() try {
  visual_failure(gneiss::engine::execution_mode::thread_pool);
  visual_failure(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  run(gneiss::engine::execution_mode::cooperative);
  shared_scene_assets(gneiss::engine::execution_mode::thread_pool);
  shared_scene_assets(gneiss::engine::execution_mode::cooperative);
  shared_texture_versions(gneiss::engine::execution_mode::thread_pool);
  shared_texture_versions(gneiss::engine::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
