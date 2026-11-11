// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/codec_result.hpp"
#include "../support/mesh_objects.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"
#include "runtime/application.hpp"

#include "../support/scene_access.hpp"
#include "engine/function/scene/world.hpp"

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
    (void)app->run(1);
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
  gneiss::runtime::scene_open_request request;
  check(gneiss::test_support::take_value(app->scenes().open("asset://scene.json"), request) ==
        gneiss::engine::error::success);
  const auto step = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    (void)app->run(1);
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
  until([&] {
    return gneiss::test_support::observe_request(request, progress) &&
           progress.phase == scene_load_phase::ready;
  });
  scene_load_completion completion;
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
  entity_id node;
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*app}->find_node(
                completion.objects,
                *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000002")),
            node) == gneiss::engine::error::success);
  gneiss::editor::scene_mesh_binding binding;
  check(gneiss::test_support::take_value(
            gneiss::test_support::read_mesh(*app->native_world(), completion.scene, node),
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
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*app}->get_node_count(completion.scene), nodes) ==
            gneiss::engine::error::success &&
        nodes == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  const std::array sources{gneiss::engine::render_resource_source{
      "asset://material.json", gneiss::engine::render_resource_kind::material}};
  gneiss::runtime::asset_batch_request assets_request;
  check(gneiss::test_support::take_value(app->load_assets(sources, true), assets_request) ==
        gneiss::engine::error::success);
  gneiss::runtime::asset_load_completion loaded;
  until([&] {
    bool finished{};
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(assets_request, loaded, finished) ==
          gneiss::engine::error::success);
    return finished;
  });
  check(loaded.result == gneiss::engine::error::success &&
        gneiss::engine::resource_cast<gneiss::engine::material>(loaded.assets.front()) == material);
  check(gneiss::test_support::take_value(
            gneiss::test_support::read_mesh(*app->native_world(), completion.scene, node),
            binding) == gneiss::engine::error::success &&
        binding.material == material);
  // 引用型纹理依赖不阻止材质完成；旧后端快照保持可用。
  const auto old_data = app->render().share_material(engine::resource_handle(material));
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["asset://missing-update.ktx2","","","",""]})";
  check(gneiss::test_support::take_value(app->load_assets(sources, true), assets_request).ok());
  until([&] {
    bool finished{};
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(assets_request, loaded, finished).ok());
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
  auto unattached = app->scenes().open("asset://scene.json");
  check(!unattached && unattached.error() == gneiss::engine::error::not_ready);
  gneiss::runtime::scene_open_request empty;
  check(!empty.progress() && empty.cancel() == gneiss::engine::error::invalid_state);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  gneiss::engine::world* old_world{};
  check((old_world = app->native_world()) != nullptr);
  auto old_scene = old_world->create_scene();
  check(old_scene.has_value());
  check(old_scene->create_object().has_value());
  constexpr std::string_view uri = "asset://scene.json";
  gneiss::runtime::scene_open_request request;
  check(gneiss::test_support::take_value(app->scenes().open(uri), request) ==
        gneiss::engine::error::success);
  gneiss::runtime::scene_open_request rejected;
  check(gneiss::test_support::take_value(app->scenes().open(uri), rejected) ==
            gneiss::engine::error::not_ready &&
        !rejected);
  // 查询不能推进加载；新请求仍停留在初始阶段。
  scene_load_progress before_query;
  check(gneiss::test_support::observe_request(request, before_query));
  for (int query = 0; query < 3; ++query) {
    scene_load_completion observed;
    bool terminal{};
    check(gneiss::test_support::receive_request(request, observed, terminal).ok() && !terminal);
    scene_load_progress after_query;
    check(gneiss::test_support::observe_request(request, after_query));
    check(after_query.phase == before_query.phase &&
          after_query.completed == before_query.completed);
  }
  auto drive = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    scene_load_completion completion;
    bool ready{};
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(request, completion, ready) ==
          gneiss::engine::error::success);
    std::this_thread::yield();
    return std::pair{ready, std::move(completion)};
  };
  auto await_phase = [&](scene_load_phase phase) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      auto [finished, completion] = drive();
      check(!finished);
      scene_load_progress progress;
      check(gneiss::test_support::observe_request(request, progress));
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
  check(gneiss::test_support::assign_value(
            app->scenes().activate(gneiss::runtime::scene_open_request{}), completion) ==
        gneiss::engine::error::invalid_state);
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
  check(completion.scene != gneiss::scene_instance_id{} &&
        completion.progress.phase == scene_load_phase::applied);
  check(completion.source && completion.content && completion.content->description &&
        completion.content == completion.source->content() && !completion.bindings.empty());
  check((observed = app->native_world()) != nullptr && observed != old_world);
  check(empty.cancel() == gneiss::engine::error::invalid_state);
  // 领取回执不退休旧 World；退休只发生在下一次显式推进。
  const auto retired_before = app->scene_retirement().retired_domains;
  bool no_terminal{};
  scene_load_completion no_completion;
  check(gneiss::test_support::receive_request(request, no_completion, no_terminal).ok() &&
        !no_terminal);
  check(app->scene_retirement().retired_domains == retired_before);
  // 激活后的旧域由 session 延迟退役，期间不能接收下一次打开。
  check(gneiss::test_support::assign_value(old_world->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 1U);
  check(gneiss::test_support::take_value(app->scenes().open(uri), rejected) ==
            gneiss::engine::error::not_ready &&
        !rejected);
  (void)drive();
  // 旧域已销毁，native_world() 的借用指针不能再解引用。
  old_world = nullptr;
  check(gneiss::test_support::assign_value(app->native_world()->entity_count(), count).ok());

  // ready 阶段仍可取消，不改写活动域；迟到请求身份不能取消后续请求。
  check(gneiss::test_support::take_value(app->scenes().open(uri), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(empty.cancel() == gneiss::engine::error::invalid_state);
  check(request.cancel() == gneiss::engine::error::success);
  auto [finished, cancelled] = drive();
  check(finished && cancelled.progress.phase == scene_load_phase::cancelled);
  check(!cancelled.source && !cancelled.content && cancelled.bindings.empty());
  check(cancelled.progress.application_logical_bytes <=
        gneiss::engine::render_service::default_memory_limit);
  check((old_world = app->native_world()) != nullptr && old_world == observed);

  // 同步实例化完成后不复读来源；当前实例使用自己的快照，后续请求才重新读取。
  check(gneiss::test_support::take_value(app->scenes().open(uri), request).ok());
  (void)await_phase(scene_load_phase::ready);
  std::ofstream(files.root / "scene.json", std::ios::app) << ' ';
  (void)await_phase(scene_load_phase::ready);
  check(request.cancel().ok());
  check(drive().first);
  check(gneiss::test_support::assign_value(observed->entity_count(), count).ok() && count == 1U);

  // 视觉来源损坏不阻止结构 ready；取消仍保持旧 World。
  std::ofstream(files.root / "material.json") << "broken";
  check(gneiss::test_support::take_value(app->scenes().open(uri), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(request.cancel() == gneiss::engine::error::success);
  check(drive().first);
  check(gneiss::test_support::assign_value(observed->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  check(gneiss::test_support::take_value(app->scenes().open(uri), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
  (void)drive();
  // ready 只完成结构，取消仍必须保留已经激活的 World。
  check((observed = app->native_world()) != nullptr);
  for (const auto phase : {scene_load_phase::preparing, scene_load_phase::ready}) {
    check(gneiss::test_support::take_value(app->scenes().open(uri), request) ==
          gneiss::engine::error::success);
    if (phase != scene_load_phase::preparing) {
      (void)await_phase(phase);
    }
    check(request.cancel() == gneiss::engine::error::success);
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
  check(gneiss::test_support::take_value(app->scenes().open("asset://empty.scene"), request) ==
        gneiss::engine::error::success);
  (void)await_phase(scene_load_phase::ready);
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
  (void)drive();
  check((observed = app->native_world()) != nullptr);
  check(gneiss::test_support::assign_value(observed->entity_count(), count) ==
            gneiss::engine::error::success &&
        count == 0U);
  check(gneiss::test_support::take_value(app->scenes().open(uri), request) ==
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
  gneiss::runtime::scene_open_request request;
  auto poll = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(request, completion, terminal) ==
          gneiss::engine::error::success);
    std::this_thread::yield();
    return terminal;
  };
  auto prepare = [&] {
    check(gneiss::test_support::take_value(app->scenes().open("asset://scene.json"), request) ==
          gneiss::engine::error::success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    scene_load_progress progress;
    do {
      check(std::chrono::steady_clock::now() < deadline && !poll());
      check(gneiss::test_support::observe_request(request, progress));
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
    gneiss::entity_id node{};
    check(gneiss::test_support::take_value(
              gneiss::test_support::scene_access{*app}->find_node(completion.objects, uuid),
              node) == gneiss::engine::error::success);
    gneiss::editor::scene_mesh_binding value;
    check(gneiss::test_support::take_value(
              gneiss::test_support::read_mesh(*app->native_world(), completion.scene, node),
              value) == gneiss::engine::error::success);
    return value;
  };
  (void)prepare();
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
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
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
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
  check(request.cancel() == gneiss::engine::error::success);
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
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
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
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           completion) == gneiss::engine::error::success);
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
  gneiss::runtime::scene_open_request request;
  check(gneiss::test_support::take_value(app->scenes().open("asset://scene.json"), request) ==
        gneiss::engine::error::success);
  scene_load_completion scene_completion;
  scene_load_progress progress;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(request, scene_completion, terminal) ==
              gneiss::engine::error::success &&
          !terminal);
    check(gneiss::test_support::observe_request(request, progress));
    std::this_thread::yield();
  } while (progress.phase != scene_load_phase::ready);
  // 材质各自一批准备，共享纹理必须复用同一 RID，绑定检查及预算去重均应成功。
  check(progress.texture_payload_bytes == 0U);
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           scene_completion) == gneiss::engine::error::success);
  drain_visuals(app, scheduler, mode);
  std::uint64_t count{};
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*app}->get_node_count(scene_completion.scene),
            count) == gneiss::engine::error::success &&
        count == 2U);
  const std::array textures{gneiss::engine::render_resource_source{
      .uri = "asset://texture.json", .type = gneiss::engine::render_resource_kind::texture}};
  gneiss::runtime::asset_batch_request assets_request;
  check(gneiss::test_support::take_value(app->load_assets(textures, true), assets_request) ==
        gneiss::engine::error::success);
  gneiss::runtime::asset_load_completion assets;
  bool ready{};
  while (!ready) {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(assets_request, assets, ready) ==
          gneiss::engine::error::success);
    std::this_thread::yield();
  }
  // 显式纹理重载仅返回该纹理，不重读材质或返回历史版本。
  check(assets.result == gneiss::engine::error::success && assets.assets.size() == 1U &&
        assets.prepared_bytes > 0U);
  // 视觉文件变化不属于场景结构快照；打开场景复用当前成功资源。
  bool ignored{};
  check(app->run(1).ok());
  check(gneiss::test_support::receive_request(request, scene_completion, ignored) ==
            gneiss::engine::error::success &&
        !ignored);
  std::ofstream(files.root / "image.ktx2", std::ios::app).put('\0');
  check(gneiss::test_support::take_value(app->scenes().open("asset://scene.json"), request) ==
        gneiss::engine::error::success);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(request, scene_completion, ignored) ==
              gneiss::engine::error::success &&
          !ignored);
    check(gneiss::test_support::observe_request(request, progress));
  } while (progress.phase != scene_load_phase::ready);
  check(gneiss::test_support::assign_value(app->scenes().activate(std::move(request)),
                                           scene_completion) == gneiss::engine::error::success);
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
    gneiss::runtime::scene_open_request request;
    gneiss::runtime::asset_batch_request assets_request;
    if (stage == 1) {
      check(
          gneiss::test_support::take_value(app->scenes().open("asset://scene.json"), request).ok());
      (void)app->run(1);
    } else if (stage == 2) {
      const std::array sources{gneiss::engine::render_resource_source{
          "asset://mesh.json", gneiss::engine::render_resource_kind::mesh}};
      check(gneiss::test_support::take_value(app->load_assets(sources, true), assets_request).ok());
      gneiss::runtime::asset_load_completion completion;
      bool ready{};
      check(app->run(1).ok());
      check(gneiss::test_support::receive_request(assets_request, completion, ready).ok());
    }
    check(app->request_shutdown().ok() && !app->accepts_requests() && closed == 0);
    check(app->request_shutdown().ok());
    check(gneiss::test_support::take_value(app->scenes().open("asset://scene.json"), request) ==
          gneiss::engine::error::invalid_state);
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
  root.components.set(e::camera_description{{1.0F, 0.1F, 100.0F}, true});
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
  gneiss::runtime::scene_open_request request;
  check(gneiss::test_support::take_value(app->scenes().open("asset://source.scene"), request).ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)scheduler.run_ready();
    (void)app->run(1);
    scene_load_progress progress;
    if (gneiss::test_support::observe_request(request, progress) &&
        progress.phase == scene_load_phase::ready)
      break;
    std::this_thread::yield();
  }
  entity_id active_camera;
  check(gneiss::engine::get_active_camera(*app->native_world(), active_camera) ==
        e::error::not_ready);
  scene_load_completion loaded;
  check(
      gneiss::test_support::assign_value(app->scenes().activate(std::move(request)), loaded).ok());
  const auto find = [](const auto& bindings, e::guid source) {
    auto found = std::ranges::find(bindings, source, &e::scene_object_binding::source);
    check(found != bindings.end());
    return found->entity;
  };
  const auto root_node = find(loaded.objects, root.uuid);
  const auto child_node = find(loaded.objects, child.uuid);
  check(gneiss::engine::get_active_camera(*app->native_world(), active_camera).ok());
  check(active_camera.get() == root_node.get());
  for (std::size_t i = 0; i < loaded.objects.size(); ++i) {
    auto info = gneiss::test_support::scene_access{*app}->get_node_info(loaded.scene, i);
    check(bool(info));
  }
  e::math::transform moved;
  moved.translation.x = 7;
  check(app->native_world()->object(entity_id{root_node.get()})->set_local_transform(moved).ok());
  const auto reload = [&] {
    (void)app->run(1);
    auto reload_request = app->scenes().reload(loaded.scene, "asset://source.prefab", true);
    check(bool(reload_request));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      check(std::chrono::steady_clock::now() < until);
      (void)scheduler.run_ready();
      (void)app->run(1);
      auto completion = reload_request->take();
      check(completion || completion.error() == e::error::not_ready);
      if (completion)
        return std::move(*completion);
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
  check(app->native_world()->find(gneiss::entity_id{(child_node).get()}).name() == "updated");
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
  entity_id parent;
  check(gneiss::test_support::assign_value(
            app->native_world()->find(gneiss::entity_id{(root_node).get()}).parent(), parent)
            .ok() &&
        parent == extra_node);
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
