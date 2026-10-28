// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"
#include "runtime/application.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"

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
        << R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[1,0,0,1]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}}],"prefab_instances":[]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void drain_visuals(const std::shared_ptr<application>& app,
                   gneiss::engine::task_scheduler& scheduler, gneiss::engine::execution_mode mode) {
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
  config.callbacks.diagnostic = [&](const auto& event) {
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
  check(gneiss::engine::asset_cast<gneiss::engine::mesh_resource>(ready) == binding.mesh);
  check(
      app->query_ready_asset({"asset://material.json", gneiss::engine::render_asset_type::material},
                             ready) == gneiss::engine::result::not_ready);
  std::uint64_t nodes{};
  check(app->scenes()->get_node_count(completion.scene, &nodes) ==
            gneiss::engine::result::success &&
        nodes == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[0,1,0,1]})";
  const std::array sources{gneiss::engine::render_asset_reload{
      "asset://material.json", gneiss::engine::render_asset_type::material}};
  check(app->request_assets(sources, 1U, 2U, request, true) == gneiss::engine::result::success);
  gneiss::engine::texture_load_completion loaded;
  until([&] {
    bool finished{};
    check(app->poll_assets(loaded, finished) == gneiss::engine::result::success);
    return finished;
  });
  check(loaded.result == gneiss::engine::result::success &&
        gneiss::engine::asset_cast<gneiss::engine::material_resource>(loaded.assets.front()) ==
            material);
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
  std::shared_ptr<application> app;
  application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(application::create(desc, app) == gneiss::engine::result::success);
  // 未装配执行器时，宿主入口应保留空闲查询与未就绪操作的不同语义。
  std::uint64_t unattached_request = 99U;
  check(app->request_scene("asset://scene.json", 1U, 1U, unattached_request) ==
            gneiss::engine::result::not_ready &&
        unattached_request == 0U);
  scene_load_progress idle_progress;
  check(!app->scene_progress(idle_progress));
  scene_load_completion idle_completion;
  bool idle_ready = true;
  check(app->poll_scene(idle_completion, idle_ready) == gneiss::engine::result::not_ready &&
        !idle_ready);
  check(app->cancel_scene(99U) == gneiss::engine::result::not_ready);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  gneiss::engine::world* old_world{};
  check((old_world = app->native_world()) != nullptr);
  std::uint64_t old_entity{};
  check(gneiss::engine::write_identifier<gneiss::entity_id>(old_entity, [&](auto& native_output) {
          return old_world->entity_create(native_output);
        }) == gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://scene.json";
  std::uint64_t request{};
  check(app->request_scene(uri, 1U, 1U, request) == gneiss::engine::result::success);
  const auto first = request;
  std::uint64_t rejected{};
  check(app->request_scene(uri, 1U, 2U, rejected) == gneiss::engine::result::not_ready &&
        rejected == 0U);
  auto drive = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    scene_load_completion completion;
    bool ready{};
    check(app->poll_scene(completion, ready) == gneiss::engine::result::success);
    std::this_thread::yield();
    return std::pair{ready, std::move(completion)};
  };
  auto await_phase = [&](scene_load_phase phase) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      auto [finished, completion] = drive();
      check(!finished);
      scene_load_progress progress;
      check(app->scene_progress(progress));
      if (progress.phase == phase) {
        return progress;
      }
    }
    throw std::runtime_error("等待场景阶段超时");
  };
  const auto prepared = await_phase(scene_load_phase::ready);
  // 结构 ready 时视觉内容尚未读取或上传。
  gneiss::engine::render_asset_ref not_ready;
  check(app->query_ready_asset({"asset://mesh.json", gneiss::engine::render_asset_type::mesh},
                               not_ready) == gneiss::engine::result::not_ready);
  check(prepared.cpu_data_bytes == prepared.resident_bytes);
  check(prepared.application_logical_bytes >= prepared.resident_bytes);
  check(prepared.application_cpu_data_bytes >= prepared.cpu_data_bytes);
  check(prepared.available_bytes +
            std::max(prepared.application_logical_bytes, prepared.application_cpu_data_bytes) ==
        gneiss::engine::render_resource_service::default_memory_limit);
  gneiss::engine::world* observed{};
  check((observed = app->native_world()) != nullptr && observed == old_world);
  std::uint64_t count{};
  check(old_world->entity_count(count) == gneiss::engine::result::success && count == 1U);
  scene_load_completion completion;
  check(app->activate_scene(first + 100U, completion) == gneiss::engine::result::not_ready);
  check(app->activate_scene(first, completion) == gneiss::engine::result::success);
  check(completion.scene != gneiss::scene_instance_id{} &&
        completion.progress.phase == scene_load_phase::applied);
  check((observed = app->native_world()) != nullptr && observed != old_world);
  check(app->cancel_scene(first) == gneiss::engine::result::not_ready);
  // 激活后的旧域由 session 延迟退役，期间不能接收下一次打开。
  check(old_world->entity_count(count) == gneiss::engine::result::success && count == 1U);
  check(app->request_scene(uri, 1U, 2U, rejected) == gneiss::engine::result::not_ready &&
        rejected == 0U);
  (void)drive();
  // 旧域已销毁，native_world() 的借用指针不能再解引用。
  old_world = nullptr;
  check(app->native_world()->entity_count(count).ok());

  // ready 阶段仍可取消，不改写活动域；迟到请求身份不能取消后续请求。
  check(app->request_scene(uri, 1U, 2U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(app->cancel_scene(first) == gneiss::engine::result::not_ready);
  check(app->cancel_scene(request) == gneiss::engine::result::success);
  auto [finished, cancelled] = drive();
  check(finished && cancelled.progress.phase == scene_load_phase::cancelled);
  check(cancelled.progress.application_logical_bytes <=
        gneiss::engine::render_resource_service::default_memory_limit);
  check((old_world = app->native_world()) != nullptr && old_world == observed);

  // 描述已准备后不复读来源；当前请求使用自己的 CPU 快照，后续请求才重新读取。
  check(app->request_scene(uri, 1U, 3U, request).ok());
  (void)await_phase(scene_load_phase::assets);
  std::ofstream(files.root / "scene.json", std::ios::app) << ' ';
  (void)await_phase(scene_load_phase::ready);
  check(app->cancel_scene(request).ok());
  check(drive().first);
  check(observed->entity_count(count).ok() && count == 1U);

  // 视觉来源损坏不阻止结构 ready；取消仍保持旧 World。
  std::ofstream(files.root / "material.json") << "broken";
  check(app->request_scene(uri, 1U, 3U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(app->cancel_scene(request) == gneiss::engine::result::success);
  check(drive().first);
  check(observed->entity_count(count) == gneiss::engine::result::success && count == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[0,1,0,1]})";
  check(app->request_scene(uri, 1U, 4U, request) == gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  (void)drive();
  // ready 只完成结构，取消仍必须保留已经激活的 World。
  check((observed = app->native_world()) != nullptr);
  for (const auto phase :
       {scene_load_phase::preparing, scene_load_phase::assets, scene_load_phase::ready}) {
    check(app->request_scene(uri, 1U, 5U, request) == gneiss::engine::result::success);
    if (phase != scene_load_phase::preparing) {
      (void)await_phase(phase);
    }
    check(app->cancel_scene(request) == gneiss::engine::result::success);
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool stopped{};
    while (!stopped && std::chrono::steady_clock::now() < cancel_deadline) {
      auto [done, cancelled_result] = drive();
      if (done) {
        check(cancelled_result.progress.phase == scene_load_phase::cancelled);
        check(cancelled_result.progress.cleanup_complete);
        stopped = true;
      }
    }
    check(stopped && observed->entity_count(count) == gneiss::engine::result::success &&
          count == 1U);
  }
  // 无渲染依赖的空场景不需要采用批次，也必须正常激活。
  std::ofstream(files.root / "empty.scene")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000003","objects":[],"prefab_instances":[]})";
  check(app->request_scene("asset://empty.scene", 1U, 6U, request) ==
        gneiss::engine::result::success);
  (void)await_phase(scene_load_phase::ready);
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  (void)drive();
  check((observed = app->native_world()) != nullptr);
  check(observed->entity_count(count) == gneiss::engine::result::success && count == 0U);
  check(app->request_scene(uri, 1U, 7U, request) == gneiss::engine::result::success);
  app.reset();
  check(scheduler.stats().retained == 0U);
  // Application 销毁后不再访问借用的 World。
  observed = nullptr;
}

void shared_scene_assets(gneiss::engine::execution_mode mode) {
  fixture files;
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<application> app;
  application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(application::create(desc, app) == gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  scene_load_completion completion;
  std::uint64_t request{};
  auto poll = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(app->poll_scene(completion, terminal) == gneiss::engine::result::success);
    std::this_thread::yield();
    return terminal;
  };
  auto prepare = [&] {
    check(app->request_scene("asset://scene.json", 1U, 1U, request) ==
          gneiss::engine::result::success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    scene_load_progress progress;
    do {
      check(std::chrono::steady_clock::now() < deadline && !poll());
      check(app->scene_progress(progress));
    } while (progress.phase != scene_load_phase::ready);
    return progress;
  };
  auto selected = [&](gneiss::engine::render_asset_type type, const char* uri) {
    gneiss::engine::render_asset_ref lease;
    check(app->query_ready_asset({.uri = uri, .type = type}, lease) ==
          gneiss::engine::result::success);
    return lease;
  };
  auto binding = [&] {
    constexpr std::string_view uuid = "00000000-0000-4000-8000-000000000002";
    gneiss::scene_node_id node{};
    check(app->scenes()->find_node(completion.scene, uuid, node) ==
          gneiss::engine::result::success);
    gneiss::engine::scene_instances::mesh_binding value;
    check(app->capture_mesh_binding(completion.scene, node, value) ==
          gneiss::engine::result::success);
    return value;
  };
  (void)prepare();
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  drain_visuals(app, scheduler, mode);
  auto original = binding();
  auto mesh = selected(gneiss::engine::render_asset_type::mesh, "asset://mesh.json");
  auto material = selected(gneiss::engine::render_asset_type::material, "asset://material.json");
  gneiss::engine::world* old_world{};
  check((old_world = app->native_world()) != nullptr);
  check(!poll());
  // 两个 World 同时持有相同版本，不重复计量或创建后端资源。
  const auto second = prepare();
  check(second.application_logical_bytes > 0U);
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  auto reused = binding();
  check(reused.mesh == gneiss::engine::asset_cast<gneiss::engine::mesh_resource>(mesh) &&
        reused.material == gneiss::engine::asset_cast<gneiss::engine::material_resource>(material));
  gneiss::engine::world* new_world{};
  check((new_world = app->native_world()) != nullptr && old_world != new_world);
  std::uint64_t count{};
  check(old_world->entity_count(count) == gneiss::engine::result::success);
  check(!poll());
  // 旧域已销毁，native_world() 的借用指针不能再解引用。
  old_world = nullptr;
  check(app->native_world()->entity_count(count).ok());
  check(new_world->entity_count(count) == gneiss::engine::result::success && count == 1U);
  // 更新完成后保持稳定资源引用；取消场景切换不撤回已更新的共享资源。
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[0,1,0,1]})";
  (void)prepare();
  check(selected(gneiss::engine::render_asset_type::material, "asset://material.json") == material);
  check(app->cancel_scene(request) == gneiss::engine::result::success);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!poll()) {
    check(std::chrono::steady_clock::now() < deadline);
  }
  check(completion.progress.phase == scene_load_phase::cancelled &&
        completion.progress.cleanup_complete);
  check(selected(gneiss::engine::render_asset_type::material, "asset://material.json") == material);
  // 重试和激活都继续复用同一资源身份。
  (void)prepare();
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  auto changed = binding();
  check(changed.mesh == gneiss::engine::asset_cast<gneiss::engine::mesh_resource>(mesh) &&
        changed.material ==
            gneiss::engine::asset_cast<gneiss::engine::material_resource>(material));
  check(gneiss::engine::asset_cast<gneiss::engine::material_resource>(
            selected(gneiss::engine::render_asset_type::material, "asset://material.json")) ==
        changed.material);
  check(!poll());
  check(original.mesh == gneiss::engine::asset_cast<gneiss::engine::mesh_resource>(mesh) &&
        original.material ==
            gneiss::engine::asset_cast<gneiss::engine::material_resource>(material));
  // 已缓存资源不因打开场景而隐式重读损坏文件；显式重载独立处理。
  check((new_world = app->native_world()) != nullptr);
  std::ofstream(files.root / "material.json") << "broken";
  (void)prepare();
  check(app->activate_scene(request, completion) == gneiss::engine::result::success);
  drain_visuals(app, scheduler, mode);
  check(gneiss::engine::asset_cast<gneiss::engine::material_resource>(
            selected(gneiss::engine::render_asset_type::material, "asset://material.json")) ==
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
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
  for (const auto* name : {"material.json", "other.json"}) {
    std::ofstream(files.root / name)
        << R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture.json"})";
  }
  std::ofstream(files.root / "scene.json")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}},)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://other.json"}}}],"prefab_instances":[]})";
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<application> app;
  application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(application::create(desc, app) == gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  std::uint64_t request{};
  check(app->request_scene("asset://scene.json", 1U, 1U, request) ==
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
    check(app->poll_scene(scene_completion, terminal) == gneiss::engine::result::success &&
          !terminal);
    check(app->scene_progress(progress));
    std::this_thread::yield();
  } while (progress.phase != scene_load_phase::ready);
  // 材质各自一批准备，共享纹理必须复用同一 RID，绑定检查及预算去重均应成功。
  check(progress.texture_payload_bytes == 0U);
  check(app->activate_scene(request, scene_completion) == gneiss::engine::result::success);
  drain_visuals(app, scheduler, mode);
  std::uint64_t count{};
  check(app->scenes()->get_node_count(scene_completion.scene, &count) ==
            gneiss::engine::result::success &&
        count == 2U);
  const std::array textures{gneiss::engine::render_asset_reload{
      .uri = "asset://texture.json", .type = gneiss::engine::render_asset_type::texture}};
  check(app->request_assets(textures, 1U, 2U, request, true) == gneiss::engine::result::success);
  gneiss::engine::texture_load_completion assets;
  bool ready{};
  while (!ready) {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->poll_assets(assets, ready) == gneiss::engine::result::success);
    std::this_thread::yield();
  }
  // 显式纹理重载仅返回该纹理，不重读材质或返回历史版本。
  check(assets.result == gneiss::engine::result::success && assets.assets.size() == 1U &&
        assets.prepared_bytes > 0U);
  // 视觉文件变化不属于场景结构快照；打开场景复用当前成功资源。
  bool ignored{};
  check(app->poll_scene(scene_completion, ignored) == gneiss::engine::result::success && !ignored);
  std::ofstream(files.root / "image.ktx2", std::ios::app).put('\0');
  check(app->request_scene("asset://scene.json", 1U, 3U, request) ==
        gneiss::engine::result::success);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->poll_scene(scene_completion, ignored) == gneiss::engine::result::success &&
          !ignored);
    check(app->scene_progress(progress));
  } while (progress.phase != scene_load_phase::ready);
  check(app->activate_scene(request, scene_completion) == gneiss::engine::result::success);
  drain_visuals(app, scheduler, mode);
}
void asynchronous_shutdown(gneiss::engine::execution_mode mode) {
  fixture files;
  for (int stage = 0; stage < 3; ++stage) {
    gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
    application_configuration config;
    const auto root = files.root.string();
    config.asset_root = root;
    unsigned closed{};
    config.callbacks.shutdown = [&] { ++closed; };
    std::shared_ptr<application> app;
    check(application::create(config, app).ok());
    check(app->attach_task_executor(scheduler).ok());
    check(app->advance_shutdown() == gneiss::engine::result::invalid_state);
    std::uint64_t request{};
    if (stage == 1) {
      check(app->request_scene("asset://scene.json", 1, 1, request).ok());
      app->advance_scene_load();
    } else if (stage == 2) {
      const std::array sources{gneiss::engine::render_asset_reload{
          "asset://mesh.json", gneiss::engine::render_asset_type::mesh}};
      check(app->request_assets(sources, 1, 1, request, true).ok());
      gneiss::engine::texture_load_completion completion;
      bool ready{};
      check(app->poll_assets(completion, ready).ok());
    }
    check(app->request_shutdown().ok() && !app->accepts_requests() && closed == 0);
    check(app->request_shutdown().ok());
    check(app->request_scene("asset://scene.json", 1, 2, request) ==
          gneiss::engine::result::invalid_state);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      const auto status = app->advance_shutdown();
      if (status != gneiss::engine::result::not_ready) {
        check(status.ok());
        break;
      }
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::engine::execution_mode::cooperative)
        (void)scheduler.run_ready();
      std::this_thread::yield();
    }
    check(app->native_world() == nullptr && closed == 1);
    check(scheduler.stats().retained == 0 && scheduler.stats().running == 0);
    check(app->advance_shutdown().ok() && closed == 1);
    app.reset();
    check(closed == 1);
  }
}

} // namespace
int main() try {
  asynchronous_shutdown(gneiss::engine::execution_mode::thread_pool);
  asynchronous_shutdown(gneiss::engine::execution_mode::cooperative);
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
