// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/codec_result.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"
#include "runtime/application.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"

#include <algorithm>
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
    check(gneiss::test_support::take_codec(gneiss::engine::encode_mesh_binary(mesh), bytes,
                                           diagnostic) == std::nullopt);
    std::ofstream output(root / "mesh.json", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::ofstream(root / "material.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://mesh.json","material":"asset://material.json","uuid":"b415ac68-8f28-4fc4-8bf1-7fb1169d485a"}}]}]})";
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

void visual_failure(gneiss::engine::execution_mode mode, bool missing_texture = false) {
  fixture files;
  if (missing_texture)
    std::ofstream(files.root / "material.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["asset://missing.ktx2","","","",""]})";
  else
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
  check(gneiss::test_support::assign_value(application::create(config), app) ==
        gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  std::uint64_t request{};
  check(gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1U, 1U),
                                         request) == gneiss::engine::error::success);
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
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  scene_node_id node;
  check(gneiss::test_support::take_value(
            app->scenes()->find_node(completion.scene, *gneiss::engine::guid::parse(
                                                           "00000000-0000-4000-8000-000000000002")),
            node) == gneiss::engine::error::success);
  gneiss::engine::scene_mesh_binding binding;
  check(
      gneiss::test_support::take_value(app->scenes()->capture_mesh_binding(completion.scene, node),
                                       binding) == gneiss::engine::error::success);
  check(binding.mesh && binding.material);
  auto material = binding.material;
  until([&] { return !app->visual_assets_loading(); });
  check(failures == (missing_texture ? 0U : 1U));
  gneiss::engine::render_resource_ref ready;
  check(gneiss::test_support::take_value(
            app->assets().find({"asset://mesh.json", gneiss::engine::render_resource_kind::mesh},
                               app->assets().profile()),
            ready) == gneiss::engine::error::success);
  check(gneiss::engine::resource_cast<gneiss::engine::mesh>(ready) == binding.mesh);
  check(gneiss::test_support::take_value(
            app->assets().find(
                {"asset://material.json", gneiss::engine::render_resource_kind::material},
                app->assets().profile()),
            ready) == (missing_texture ? engine::error::success : engine::error::not_ready));
  if (missing_texture) {
    const auto* data = app->render().get_material(engine::resource_handle(material));
    check(data && data->parameters.color.x == 1.0F);
    check(static_cast<bool>(material->textures()[0]));
    check(app->render().texture_status(engine::resource_handle(material->textures()[0])) ==
          engine::error::not_ready);
  }
  std::uint64_t nodes{};
  check(gneiss::test_support::take_value(app->scenes()->get_node_count(completion.scene), nodes) ==
            gneiss::engine::error::success &&
        nodes == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  const std::array sources{gneiss::engine::render_resource_source{
      "asset://material.json", gneiss::engine::render_resource_kind::material}};
  check(app->request_assets(sources, 1U, 2U, request, true) == gneiss::engine::error::success);
  gneiss::runtime::asset_load_completion loaded;
  until([&] {
    bool finished{};
    check(app->poll_assets(loaded, finished) == gneiss::engine::error::success);
    return finished;
  });
  check(loaded.result == gneiss::engine::error::success &&
        gneiss::engine::resource_cast<gneiss::engine::material>(loaded.assets.front()) == material);
  check(
      gneiss::test_support::take_value(app->scenes()->capture_mesh_binding(completion.scene, node),
                                       binding) == gneiss::engine::error::success &&
      binding.material == material);
  // 引用型纹理依赖不阻止材质完成；旧后端快照保持可用。
  const auto old_data = app->render().share_material(engine::resource_handle(material));
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["asset://missing-update.ktx2","","","",""]})";
  check(app->request_assets(sources, 1U, 3U, request, true).ok());
  until([&] {
    bool finished{};
    check(app->poll_assets(loaded, finished).ok());
    return finished;
  });
  check(loaded.result.ok());
  check(material->textures()[0] &&
        material->textures()[0]->source().uri == "asset://missing-update.ktx2");
  check(app->render().share_material(engine::resource_handle(material)) != old_data);
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
  check(gneiss::test_support::assign_value(application::create(desc), app) ==
        gneiss::engine::error::success);
  // 未装配执行器时，宿主入口应保留空闲查询与未就绪操作的不同语义。
  std::uint64_t unattached_request = 99U;
  check(gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1U, 1U),
                                         unattached_request) == gneiss::engine::error::not_ready &&
        unattached_request == 0U);
  scene_load_progress idle_progress;
  check(!app->scene_progress(idle_progress));
  scene_load_completion idle_completion;
  bool idle_ready = true;
  check(app->poll_scene(idle_completion, idle_ready) == gneiss::engine::error::not_ready &&
        !idle_ready);
  check(app->cancel_scene(99U) == gneiss::engine::error::not_ready);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  gneiss::engine::world* old_world{};
  check((old_world = app->native_world()) != nullptr);
  std::uint64_t old_entity{};
  check(gneiss::engine::write_identifier<gneiss::entity_id>(old_entity, [&](auto& native_output) {
          return old_world->entity_create(native_output);
        }) == gneiss::engine::error::success);
  constexpr std::string_view uri = "asset://scene.json";
  std::uint64_t request{};
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 1U), request) ==
        gneiss::engine::error::success);
  const auto first = request;
  std::uint64_t rejected{};
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 2U), rejected) ==
            gneiss::engine::error::not_ready &&
        rejected == 0U);
  auto drive = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    scene_load_completion completion;
    bool ready{};
    check(app->poll_scene(completion, ready) == gneiss::engine::error::success);
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
  gneiss::engine::render_resource_ref not_ready;
  check(gneiss::test_support::take_value(
            app->assets().find({"asset://mesh.json", gneiss::engine::render_resource_kind::mesh},
                               app->assets().profile()),
            not_ready) == gneiss::engine::error::not_ready);
  check(prepared.cpu_data_bytes == prepared.resident_bytes);
  check(prepared.application_logical_bytes >= prepared.resident_bytes);
  check(prepared.application_cpu_data_bytes >= prepared.cpu_data_bytes);
  check(prepared.available_bytes +
            std::max(prepared.application_logical_bytes, prepared.application_cpu_data_bytes) ==
        gneiss::engine::render_service::default_memory_limit);
  gneiss::engine::world* observed{};
  check((observed = app->native_world()) != nullptr && observed == old_world);
  std::uint64_t count{};
  check(gneiss::test_support::assign_value(old_world->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 1U);
  scene_load_completion completion;
  check(gneiss::test_support::assign_value(app->activate_scene(first + 100U), completion) ==
        gneiss::engine::error::not_ready);
  check(gneiss::test_support::assign_value(app->activate_scene(first), completion) ==
        gneiss::engine::error::success);
  check(completion.scene != gneiss::scene_instance_id{} &&
        completion.progress.phase == scene_load_phase::applied);
  check(completion.source && completion.content && completion.content->description &&
        completion.content == completion.source->content() && !completion.bindings.empty());
  check((observed = app->native_world()) != nullptr && observed != old_world);
  check(app->cancel_scene(first) == gneiss::engine::error::not_ready);
  // 激活后的旧域由 session 延迟退役，期间不能接收下一次打开。
  check(gneiss::test_support::assign_value(old_world->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 1U);
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 2U), rejected) ==
            gneiss::engine::error::not_ready &&
        rejected == 0U);
  (void)drive();
  // 旧域已销毁，native_world() 的借用指针不能再解引用。
  old_world = nullptr;
  check(gneiss::test_support::assign_value(app->native_world()->entity_count(), count).ok());

  // ready 阶段仍可取消，不改写活动域；迟到请求身份不能取消后续请求。
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 2U), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(app->cancel_scene(first) == gneiss::engine::error::not_ready);
  check(app->cancel_scene(request) == gneiss::engine::error::success);
  auto [finished, cancelled] = drive();
  check(finished && cancelled.progress.phase == scene_load_phase::cancelled);
  check(!cancelled.source && !cancelled.content && cancelled.bindings.empty());
  check(cancelled.progress.application_logical_bytes <=
        gneiss::engine::render_service::default_memory_limit);
  check((old_world = app->native_world()) != nullptr && old_world == observed);

  // 同步实例化完成后不复读来源；当前实例使用自己的快照，后续请求才重新读取。
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 3U), request).ok());
  (void)await_phase(scene_load_phase::ready);
  std::ofstream(files.root / "scene.json", std::ios::app) << ' ';
  (void)await_phase(scene_load_phase::ready);
  check(app->cancel_scene(request).ok());
  check(drive().first);
  check(gneiss::test_support::assign_value(observed->entity_count(), count).ok() && count == 1U);

  // 视觉来源损坏不阻止结构 ready；取消仍保持旧 World。
  std::ofstream(files.root / "material.json") << "broken";
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 3U), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(app->cancel_scene(request) == gneiss::engine::error::success);
  check(drive().first);
  check(gneiss::test_support::assign_value(observed->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 4U), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  (void)drive();
  // ready 只完成结构，取消仍必须保留已经激活的 World。
  check((observed = app->native_world()) != nullptr);
  for (const auto phase : {scene_load_phase::preparing, scene_load_phase::ready}) {
    check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 5U), request) ==
          gneiss::engine::error::success);
    if (phase != scene_load_phase::preparing) {
      (void)await_phase(phase);
    }
    check(app->cancel_scene(request) == gneiss::engine::error::success);
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
    check(stopped &&
          gneiss::test_support::assign_value(observed->entity_count(), count) ==
              gneiss::engine::error::success &&
          count == 1U);
  }
  // 无渲染依赖的空场景不需要采用批次，也必须正常激活。
  std::ofstream(files.root / "empty.scene")
      << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000003","objects":[]})";
  check(gneiss::test_support::take_value(app->request_scene("asset://empty.scene", 1U, 6U),
                                         request) == gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  (void)drive();
  check((observed = app->native_world()) != nullptr);
  check(gneiss::test_support::assign_value(observed->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 0U);
  check(gneiss::test_support::take_value(app->request_scene(uri, 1U, 7U), request) ==
        gneiss::engine::error::success);
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
  check(gneiss::test_support::assign_value(application::create(desc), app) ==
        gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  scene_load_completion completion;
  std::uint64_t request{};
  auto poll = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(app->poll_scene(completion, terminal) == gneiss::engine::error::success);
    std::this_thread::yield();
    return terminal;
  };
  auto prepare = [&] {
    check(gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1U, 1U),
                                           request) == gneiss::engine::error::success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    scene_load_progress progress;
    do {
      check(std::chrono::steady_clock::now() < deadline && !poll());
      check(app->scene_progress(progress));
    } while (progress.phase != scene_load_phase::ready);
    return progress;
  };
  auto selected = [&](gneiss::engine::render_resource_kind type, const char* uri) {
    gneiss::engine::render_resource_ref lease;
    check(gneiss::test_support::take_value(
              app->assets().find({.uri = uri, .type = type}, app->assets().profile()), lease) ==
          gneiss::engine::error::success);
    return lease;
  };
  auto binding = [&] {
    const gneiss::engine::guid uuid =
        *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000002");
    gneiss::scene_node_id node{};
    check(gneiss::test_support::take_value(app->scenes()->find_node(completion.scene, uuid),
                                           node) == gneiss::engine::error::success);
    gneiss::engine::scene_mesh_binding value;
    check(gneiss::test_support::take_value(
              app->scenes()->capture_mesh_binding(completion.scene, node), value) ==
          gneiss::engine::error::success);
    return value;
  };
  (void)prepare();
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  drain_visuals(app, scheduler, mode);
  auto original = binding();
  auto mesh = selected(gneiss::engine::render_resource_kind::mesh, "asset://mesh.json");
  auto material = selected(gneiss::engine::render_resource_kind::material, "asset://material.json");
  gneiss::engine::world* old_world{};
  check((old_world = app->native_world()) != nullptr);
  check(!poll());
  // 两个 World 同时持有相同版本，不重复计量或创建后端资源。
  const auto second = prepare();
  check(second.application_logical_bytes > 0U);
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  auto reused = binding();
  check(reused.mesh == gneiss::engine::resource_cast<gneiss::engine::mesh>(mesh) &&
        reused.material == gneiss::engine::resource_cast<gneiss::engine::material>(material));
  gneiss::engine::world* new_world{};
  check((new_world = app->native_world()) != nullptr && old_world != new_world);
  std::uint64_t count{};
  check(gneiss::test_support::assign_value(old_world->entity_count(), count) ==
        gneiss::engine::error::success);
  check(!poll());
  // 旧域已销毁，native_world() 的借用指针不能再解引用。
  old_world = nullptr;
  check(gneiss::test_support::assign_value(app->native_world()->entity_count(), count).ok());
  check(gneiss::test_support::assign_value(new_world->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 1U);
  // 更新完成后保持稳定资源引用；取消场景切换不撤回已更新的共享资源。
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  (void)prepare();
  check(selected(gneiss::engine::render_resource_kind::material, "asset://material.json") ==
        material);
  check(app->cancel_scene(request) == gneiss::engine::error::success);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!poll()) {
    check(std::chrono::steady_clock::now() < deadline);
  }
  check(completion.progress.phase == scene_load_phase::cancelled &&
        completion.progress.cleanup_complete);
  check(selected(gneiss::engine::render_resource_kind::material, "asset://material.json") ==
        material);
  // 重试和激活都继续复用同一资源身份。
  (void)prepare();
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  auto changed = binding();
  check(changed.mesh == gneiss::engine::resource_cast<gneiss::engine::mesh>(mesh) &&
        changed.material == gneiss::engine::resource_cast<gneiss::engine::material>(material));
  check(gneiss::engine::resource_cast<gneiss::engine::material>(
            selected(gneiss::engine::render_resource_kind::material, "asset://material.json")) ==
        changed.material);
  check(!poll());
  check(original.mesh == gneiss::engine::resource_cast<gneiss::engine::mesh>(mesh) &&
        original.material == gneiss::engine::resource_cast<gneiss::engine::material>(material));
  // 已缓存资源不因打开场景而隐式重读损坏文件；显式重载独立处理。
  check((new_world = app->native_world()) != nullptr);
  std::ofstream(files.root / "material.json") << "broken";
  (void)prepare();
  check(gneiss::test_support::assign_value(app->activate_scene(request), completion) ==
        gneiss::engine::error::success);
  drain_visuals(app, scheduler, mode);
  check(gneiss::engine::resource_cast<gneiss::engine::material>(
            selected(gneiss::engine::render_resource_kind::material, "asset://material.json")) ==
        changed.material);
}

void shared_texture_versions(gneiss::engine::execution_mode mode) {
  fixture files;
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::test_support::take_codec(
            gneiss::engine::encode_texture_ktx2({
                .transfer = gneiss::engine::texture_transfer::srgb,
                .levels =
                    {
                        {
                            .width = 1U,
                            .height = 1U,
                            .pixels = std::vector<std::byte>(4U, std::byte{255}),
                        },
                    },
            }),
            bytes, diagnostic) == std::nullopt);
  {
    std::ofstream output(files.root / "image.ktx2", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::ofstream(files.root / "texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
  for (const auto* name : {"material.json", "other.json"}) {
    std::ofstream(files.root / name)
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["asset://texture.json","","","",""]})";
  }
  std::ofstream(files.root / "scene.json")
      << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://mesh.json","material":"asset://material.json","uuid":"3c65d64c-c47c-40f1-b0d6-34af2619a4eb"}}]},)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000003","parent":{"object":null,"source":null},"transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://mesh.json","material":"asset://other.json","uuid":"8bc198c9-d44b-4726-8080-c13a800efd97"}}]}]})";
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<application> app;
  application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(gneiss::test_support::assign_value(application::create(desc), app) ==
        gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  std::uint64_t request{};
  check(gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1U, 1U),
                                         request) == gneiss::engine::error::success);
  scene_load_completion scene_completion;
  scene_load_progress progress;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(app->poll_scene(scene_completion, terminal) == gneiss::engine::error::success &&
          !terminal);
    check(app->scene_progress(progress));
    std::this_thread::yield();
  } while (progress.phase != scene_load_phase::ready);
  // 材质各自一批准备，共享纹理必须复用同一 RID，绑定检查及预算去重均应成功。
  check(progress.texture_payload_bytes == 0U);
  check(gneiss::test_support::assign_value(app->activate_scene(request), scene_completion) ==
        gneiss::engine::error::success);
  drain_visuals(app, scheduler, mode);
  std::uint64_t count{};
  check(gneiss::test_support::take_value(app->scenes()->get_node_count(scene_completion.scene),
                                         count) == gneiss::engine::error::success &&
        count == 2U);
  const std::array textures{gneiss::engine::render_resource_source{
      .uri = "asset://texture.json", .type = gneiss::engine::render_resource_kind::texture}};
  check(app->request_assets(textures, 1U, 2U, request, true) == gneiss::engine::error::success);
  gneiss::runtime::asset_load_completion assets;
  bool ready{};
  while (!ready) {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->poll_assets(assets, ready) == gneiss::engine::error::success);
    std::this_thread::yield();
  }
  // 显式纹理重载仅返回该纹理，不重读材质或返回历史版本。
  check(assets.result == gneiss::engine::error::success && assets.assets.size() == 1U &&
        assets.prepared_bytes > 0U);
  // 视觉文件变化不属于场景结构快照；打开场景复用当前成功资源。
  bool ignored{};
  check(app->poll_scene(scene_completion, ignored) == gneiss::engine::error::success && !ignored);
  std::ofstream(files.root / "image.ktx2", std::ios::app).put('\0');
  check(gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1U, 3U),
                                         request) == gneiss::engine::error::success);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->poll_scene(scene_completion, ignored) == gneiss::engine::error::success && !ignored);
    check(app->scene_progress(progress));
  } while (progress.phase != scene_load_phase::ready);
  check(gneiss::test_support::assign_value(app->activate_scene(request), scene_completion) ==
        gneiss::engine::error::success);
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
    check(gneiss::test_support::assign_value(application::create(config), app).ok());
    check(app->attach_task_executor(scheduler).ok());
    check(app->advance_shutdown() == gneiss::engine::error::invalid_state);
    std::uint64_t request{};
    if (stage == 1) {
      check(
          gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1, 1), request)
              .ok());
      app->advance_scene_load();
    } else if (stage == 2) {
      const std::array sources{gneiss::engine::render_resource_source{
          "asset://mesh.json", gneiss::engine::render_resource_kind::mesh}};
      check(app->request_assets(sources, 1, 1, request, true).ok());
      gneiss::runtime::asset_load_completion completion;
      bool ready{};
      check(app->poll_assets(completion, ready).ok());
    }
    check(app->request_shutdown().ok() && !app->accepts_requests() && closed == 0);
    check(app->request_shutdown().ok());
    check(gneiss::test_support::take_value(app->request_scene("asset://scene.json", 1, 2),
                                           request) == gneiss::engine::error::invalid_state);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      const auto status = app->advance_shutdown();
      if (status != gneiss::engine::error::not_ready) {
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

void source_reload_without_world_prefab_state() {
  namespace e = gneiss::engine;
  fixture files;
  e::task_scheduler scheduler({.mode = e::execution_mode::cooperative});
  std::shared_ptr<application> app;
  application_configuration config;
  const auto directory = files.root.string();
  config.asset_root = directory;
  check(gneiss::test_support::assign_value(application::create(config), app).ok());
  check(app->attach_task_executor(scheduler).ok());
  const auto write = [&](std::string_view name, const auto& data) {
    auto encoded = e::asset_file::serialize_bytes(data);
    check(bool(encoded));
    std::ofstream file(files.root / name, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(encoded->data()),
               static_cast<std::streamsize>(encoded->size()));
    check(bool(file));
  };
  e::prefab_description prefab;
  prefab.uuid = e::guid::generate();
  e::game_object_description root, child;
  root.uuid = e::guid::generate();
  root.name = "root";
  child.uuid = e::guid::generate();
  child.parent.object = root.uuid;
  child.name = "child";
  prefab.root_uuid = root.uuid;
  prefab.objects = {root, child};
  write("source.prefab", prefab);
  e::scene_description document;
  document.uuid = e::guid::generate();
  e::game_object_description instance;
  instance.uuid = e::guid::generate();
  instance.prefab = e::prefab_reference_description{"asset://source.prefab", {}, {}};
  document.objects.push_back(instance);
  write("source.scene", document);
  std::uint64_t request{};
  check(gneiss::test_support::take_value(app->request_scene("asset://source.scene", 1, 1), request)
            .ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)scheduler.run_ready();
    app->advance_scene_load();
    scene_load_progress progress;
    if (app->scene_progress(progress) && progress.phase == scene_load_phase::ready)
      break;
    std::this_thread::yield();
  }
  scene_load_completion loaded;
  check(gneiss::test_support::assign_value(app->activate_scene(request), loaded).ok());
  const auto find = [](const auto& bindings, e::guid source) {
    auto found = std::ranges::find(bindings, source, &e::scene_object_binding::source);
    check(found != bindings.end());
    return found->node;
  };
  const auto root_node = find(loaded.objects, root.uuid);
  const auto child_node = find(loaded.objects, child.uuid);
  for (std::size_t i = 0; i < loaded.objects.size(); ++i) {
    auto info = app->scenes()->get_node_info(loaded.scene, i);
    check(bool(info));
  }
  e::math::transform moved;
  moved.translation.x = 7;
  check(app->scenes()->set_node_transform(loaded.scene, root_node, moved).ok());
  const auto reload = [&] {
    app->advance_scene_load();
    check(app->request_scene_reload(loaded.scene, "asset://source.prefab", true).ok());
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      check(std::chrono::steady_clock::now() < until);
      (void)scheduler.run_ready();
      auto completion = app->poll_scene_reload();
      check(bool(completion));
      if (*completion)
        return std::move(**completion);
      std::this_thread::yield();
    }
  };
  prefab.objects[1].name = "updated";
  e::game_object_description extra;
  extra.uuid = e::guid::generate();
  extra.parent.object = root.uuid;
  prefab.objects.push_back(extra);
  write("source.prefab", prefab);
  auto changed = reload();
  check(changed.result.ok() && changed.objects.size() == 3);
  check(find(changed.objects, root.uuid) == root_node &&
        find(changed.objects, child.uuid) == child_node);
  check(app->native_world()->node_identity(child_node)->name == "updated");
  auto object = app->native_world()->object(entity_id{root_node.get()});
  check(object && object->local_transform()->translation.x == 7);
  prefab.objects.erase(prefab.objects.begin() + 1);
  write("source.prefab", prefab);
  changed = reload();
  check(changed.result.ok() && changed.objects.size() == 2 &&
        !app->native_world()->object(entity_id{child_node.get()}));
  const auto extra_node = find(changed.objects, extra.uuid);
  prefab.root_uuid = extra.uuid;
  prefab.objects[0].parent.object = extra.uuid;
  prefab.objects[1].parent.object = {};
  write("source.prefab", prefab);
  changed = reload();
  check(changed.result.ok() && find(changed.objects, root.uuid) == root_node &&
        find(changed.objects, extra.uuid) == extra_node);
  scene_node_id parent;
  check(app->native_world()->node_get_parent(root_node, parent).ok() && parent == extra_node);
  std::ofstream(files.root / "source.prefab") << "broken";
  check(reload().result.failed());
  write("source.prefab", prefab);
  check(reload().result.ok());
  check(app->shutdown().ok());
}

} // namespace
int main() try {
  source_reload_without_world_prefab_state();
  asynchronous_shutdown(gneiss::engine::execution_mode::thread_pool);
  asynchronous_shutdown(gneiss::engine::execution_mode::cooperative);
  visual_failure(gneiss::engine::execution_mode::thread_pool);
  visual_failure(gneiss::engine::execution_mode::cooperative);
  visual_failure(gneiss::engine::execution_mode::thread_pool, true);
  visual_failure(gneiss::engine::execution_mode::cooperative, true);
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
