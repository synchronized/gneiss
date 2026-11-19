// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/codec_result.hpp"
#include "../../support/value_result.hpp"
#include "function/scene/detail/identifier_conversion.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/mesh/mesh_binary.hpp>
#include <gneiss/engine/resource/texture/texture_ktx2.hpp>

#include <gneiss/engine/function/scene/world.hpp>

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
    gneiss::mesh_binary_data mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0}, {0, 0, 1}},
                     {{1, 0, 0}, {1, 0}, {0, 0, 1}},
                     {{0, 1, 0}, {0, 1}, {0, 0, 1}}};
    mesh.indices = {0, 1, 2};
    mesh.tangents.assign(3, {1, 0, 0, 1});
    mesh.uv1.assign(3, {0.5F, 0.5F});
    mesh.colors.assign(3, {1, 1, 1, 1});
    std::vector<std::byte> bytes;
    gneiss::mesh_binary_diagnostic diagnostic;
    check(gneiss::test_support::take_codec(gneiss::encode_mesh_binary(mesh), bytes, diagnostic) ==
          std::nullopt);
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
gneiss::asset_load_completion drain_visuals(const std::shared_ptr<application>& app,
                                            gneiss::tasks::scheduler& scheduler,
                                            gneiss::tasks::execution_mode mode,
                                            gneiss::asset_batch_request& request) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!request.ready()) {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == gneiss::tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(app->step().ok());
    std::this_thread::yield();
  }
  auto completed = request.take();
  check(bool(completed));
  return std::move(*completed);
}

void visual_failure(gneiss::tasks::execution_mode mode, bool missing_texture = false) {
  fixture files;
  if (missing_texture)
    std::ofstream(files.root / "material.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["asset://missing.ktx2","","","",""]})";
  else
    std::ofstream(files.root / "material.json") << "broken";
  gneiss::tasks::scheduler scheduler({.workers = 1U, .mode = mode});
  application_configuration config;
  const auto root = files.root.string();
  config.asset_root = root;
  config.executor = &scheduler;
  std::shared_ptr<application> app;
  check(gneiss::test_support::assign_value(application::create(config), app) ==
        gneiss::error::success);
  check(app->step().ok());

  auto request = app->assets().load<gneiss::scene>("asset://scene.json");
  check(bool(request));
  const auto step = [&] {
    if (mode == gneiss::tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    (void)app->step();
    std::this_thread::yield();
  };
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      step();
    }
  };
  until([&] { return request->status() != gneiss::error::not_ready; });
  auto content = request->snapshot();
  check(bool(content));
  auto created_world = create_native();
  check(bool(created_world));
  auto* world = created_world->get();
  auto activated = test_support::activate_scene(*app, *world, *content);
  check(bool(activated));
  const auto& completion = activated->instance;
  check(completion.objects.size() == 1);
  const auto& source = completion.objects.front();
  check(source.object == *guid::parse("00000000-0000-4000-8000-000000000002") && !source.source);
  const auto object = world->find(source.entity);
  check(bool(object));
  const auto* component = object.try_component<mesh_component>();
  check(component && component->mesh && component->material);
  // 保存资源引用副本，不跨异步推进保留组件借用。
  auto binding = *component;
  auto material = binding.material;
  auto visual_result = drain_visuals(app, scheduler, mode, activated->visuals);
  check(visual_result.result.ok() == missing_texture);
  gneiss::render_resource_ref ready;
  check(gneiss::test_support::take_value(
            app->assets().find({"asset://mesh.json", gneiss::render_resource_kind::mesh},
                               app->assets().profile()),
            ready) == gneiss::error::success);
  check(gneiss::resource_cast<gneiss::mesh>(ready) == binding.mesh);
  check(gneiss::test_support::take_value(
            app->assets().find({"asset://material.json", gneiss::render_resource_kind::material},
                               app->assets().profile()),
            ready) == (missing_texture ? gneiss::error::success : gneiss::error::not_ready));
  if (missing_texture) {
    const auto* data = app->render().get_material(gneiss::resource_handle(material));
    check(data && data->parameters.color.x == 1.0F);
    check(static_cast<bool>(material->textures()[0]));
    check(app->render().texture_status(gneiss::resource_handle(material->textures()[0])) ==
          gneiss::error::not_ready);
  }
  auto nodes = completion.instance.size();
  check(nodes == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  const std::array sources{gneiss::render_resource_source{"asset://material.json",
                                                          gneiss::render_resource_kind::material}};
  gneiss::asset_batch_request assets_request;
  check(gneiss::test_support::take_value(app->assets().load_batch(sources, true), assets_request) ==
        gneiss::error::success);
  gneiss::asset_load_completion loaded;
  until([&] {
    bool finished{};
    check(app->step().ok());
    check(gneiss::test_support::receive_request(assets_request, loaded, finished) ==
          gneiss::error::success);
    return finished;
  });
  check(loaded.result == gneiss::error::success &&
        gneiss::resource_cast<gneiss::material>(loaded.assets.front()) == material);
  const auto* updated = object.try_component<mesh_component>();
  check(updated && updated->material == material);
  // 引用型纹理依赖不阻止材质完成；旧后端快照保持可用。
  const auto old_data = app->render().share_material(gneiss::resource_handle(material));
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["asset://missing-update.ktx2","","","",""]})";
  check(gneiss::test_support::take_value(app->assets().load_batch(sources, true), assets_request)
            .ok());
  until([&] {
    bool finished{};
    check(app->step().ok());
    check(gneiss::test_support::receive_request(assets_request, loaded, finished).ok());
    return finished;
  });
  check(loaded.result.ok());
  check(material->textures()[0] &&
        material->textures()[0]->source().uri == "asset://missing-update.ktx2");
  check(app->render().share_material(gneiss::resource_handle(material)) != old_data);
  // 已激活场景继续持有稳定资源，关闭会等待所属任务并释放其作用域。
  binding = {};
  material = {};
  loaded = {};
  ready = {};
  app.reset();
  check(scheduler.stats().retained == 0U);
}

// CPU 资产加载不修改 World；持有的固定内容不因磁盘后续变化而被重新解释。
void scene_content_boundary(gneiss::tasks::execution_mode mode) {
  namespace e = gneiss;
  fixture files;
  gneiss::tasks::scheduler scheduler({.workers = 1U, .mode = mode});
  application_configuration config;
  const auto asset_root = files.root.string();
  config.asset_root = asset_root;
  config.executor = &scheduler;
  auto created = application::create(config);
  check(bool(created));
  auto app = *created;
  check(app->step().ok());

  auto created_world = create_native();
  check(bool(created_world));
  auto* world = created_world->get();
  auto original = world->create_scene();
  check(bool(original));
  auto object = original->create_object();
  check(bool(object));
  const auto tick = [&] {
    if (mode == gneiss::tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(app->step().ok());
    std::this_thread::yield();
  };
  const auto await = [&](auto&& ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
      check(std::chrono::steady_clock::now() < deadline);
      tick();
    }
  };
  auto request = app->assets().load<e::scene>("asset://scene.json");
  check(bool(request));
  await([&] { return request->status() != e::error::not_ready; });
  check(request->status().ok());
  auto content = request->snapshot();
  check(bool(content));
  request->cancel();
  check(original->is_valid() && object->is_valid());
  check(world->entity_count() == 1);
  std::ofstream(files.root / "scene.json") << "broken";
  auto activated = gneiss::test_support::activate_scene(*app, *world, *content);
  check(bool(activated));
  await([&] { return !activated->visuals || activated->visuals.ready(); });
  if (activated->visuals) {
    auto completed = activated->visuals.take();
    check(completed && completed->result.ok());
  }
  check(activated->instance.instance.owner() == world && object->is_valid());
  auto current = e::scene_instance::borrow(*world, (activated->instance.instance).id());
  check(current && current->size() == content->description->objects.size());
  original->unload();
  check(!object->is_valid() && current->is_valid());
  auto reload = app->assets().load<e::scene>("asset://scene.json", {.reload = true});
  check(bool(reload));
  await([&] { return reload->status() != e::error::not_ready; });
  check(reload->status().failed() && current->is_valid());

  // 空内容可以实例化为空归属组，不影响既有场景。
  e::scene_content empty;
  empty.description = e::make_ref<e::scene_description>();
  auto empty_scene = gneiss::test_support::activate_scene(*app, *world, empty);
  check(bool(empty_scene) && !empty_scene->visuals);
  auto empty_instance = e::scene_instance::borrow(*world, (empty_scene->instance.instance).id());
  check(empty_instance && empty_instance->size() == 0 && current->is_valid());
}

void shared_scene_assets(gneiss::tasks::execution_mode mode) {
  fixture files;
  gneiss::tasks::scheduler scheduler({.workers = 1U, .mode = mode});
  const auto root = files.root.string();
  application_configuration config;
  config.asset_root = root;
  config.executor = &scheduler;
  auto created = application::create(config);
  check(bool(created));
  auto app = *created;
  check(app->step().ok());
  auto created_world = create_native();
  check(bool(created_world));
  auto* world = created_world->get();
  auto load = [&] {
    scene_instance id;
    check(test_support::load_scene(*app, *world, "asset://scene.json", &id, &scheduler).ok());
    auto scene = gneiss::scene_instance::borrow(*world, (id).id());
    check(bool(scene));
    return *scene;
  };
  auto first = load();
  auto mesh = app->assets().find<gneiss::mesh>("asset://mesh.json");
  auto material = app->assets().find<gneiss::material>("asset://material.json");
  check(mesh && material);
  auto second = load();
  check(*app->assets().find<gneiss::mesh>("asset://mesh.json") == *mesh);
  check(*app->assets().find<gneiss::material>("asset://material.json") == *material);
  first.unload();
  check(second.is_valid() && second.size() == 1);
  // 另一场景仍持有共享资源；普通打开不隐式重读损坏源文件。
  std::ofstream(files.root / "material.json") << "broken";
  auto third = load();
  check(third.is_valid());
  check(*app->assets().find<gneiss::material>("asset://material.json") == *material);
}

void shared_texture_versions(gneiss::tasks::execution_mode mode) {
  fixture files;
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::test_support::take_codec(
            gneiss::encode_texture_ktx2({
                .transfer = gneiss::texture_transfer::srgb,
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
  gneiss::tasks::scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<application> app;
  application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  desc.executor = &scheduler;
  check(gneiss::test_support::assign_value(application::create(desc), app) ==
        gneiss::error::success);
  check(app->step().ok());

  auto created_world = create_native();
  check(bool(created_world));
  auto* world = created_world->get();
  scene_instance scene_id;
  check(test_support::load_scene(*app, *world, "asset://scene.json", &scene_id, &scheduler).ok());
  auto first = app->assets().find<gneiss::material>("asset://material.json");
  auto second = app->assets().find<gneiss::material>("asset://other.json");
  check(first && second);
  auto texture = (*first)->textures()[0];
  check(texture && texture == (*second)->textures()[0]);
  const std::array textures{gneiss::render_resource_source{
      .uri = "asset://texture.json", .type = gneiss::render_resource_kind::texture}};
  gneiss::asset_batch_request assets_request;
  check(gneiss::test_support::take_value(app->assets().load_batch(textures, true),
                                         assets_request) == gneiss::error::success);
  auto assets = drain_visuals(app, scheduler, mode, assets_request);
  // 显式重载更新同一纹理身份，两份材质无需重建引用。
  check(assets.result.ok() && assets.assets.size() == 1);
  check(gneiss::resource_cast<gneiss::texture>(assets.assets.front()) == texture);
  check((*first)->textures()[0] == texture && (*second)->textures()[0] == texture);
  std::ofstream(files.root / "image.ktx2", std::ios::trunc) << "broken";
  check(test_support::load_scene(*app, *world, "asset://scene.json", &scene_id, &scheduler).ok());
  check(*app->assets().find<gneiss::texture>("asset://texture.json") == texture);
}

} // namespace
int main() try {
  visual_failure(gneiss::tasks::execution_mode::thread_pool);
  visual_failure(gneiss::tasks::execution_mode::cooperative);
  visual_failure(gneiss::tasks::execution_mode::thread_pool, true);
  visual_failure(gneiss::tasks::execution_mode::cooperative, true);
  scene_content_boundary(gneiss::tasks::execution_mode::thread_pool);
  scene_content_boundary(gneiss::tasks::execution_mode::cooperative);
  shared_scene_assets(gneiss::tasks::execution_mode::thread_pool);
  shared_scene_assets(gneiss::tasks::execution_mode::cooperative);
  shared_texture_versions(gneiss::tasks::execution_mode::thread_pool);
  shared_texture_versions(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
