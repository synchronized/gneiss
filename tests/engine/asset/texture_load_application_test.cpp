// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/codec_result.hpp"
#include <gneiss/engine/function/scene/components.hpp>
#include "../../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/texture/texture_ktx2.hpp>

#include "../../support/scene_access.hpp"
#include <gneiss/engine/function/application/application.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>

namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("Application 纹理注入失败，行=" + std::to_string(where.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-texture-app-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() { std::filesystem::create_directories(root); }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void run(gneiss::tasks::execution_mode mode) {
  using namespace gneiss;
  fixture files;
  std::ofstream(files.root / "a.texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::test_support::take_codec(
            gneiss::encode_texture_ktx2(
                {.transfer = gneiss::texture_transfer::srgb,
                 .levels = {{.width = 1U,
                             .height = 1U,
                             .pixels = std::vector<std::byte>(4U, std::byte{255})}}}),
            bytes, diagnostic) == std::nullopt);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  gneiss::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.asset_root = root;
  check(gneiss::test_support::assign_value(runtime::application::create(desc), app) ==
        gneiss::error::success);
  gneiss::asset_batch_request empty;
  check(empty.cancel() == gneiss::error::invalid_state);
  check(app->attach_task_executor(scheduler).ok());
  check(app->attach_task_executor(scheduler) == gneiss::error::invalid_state);
  const std::array uris{gneiss::render_resource_source{
      .uri = "asset://a.texture.json", .type = gneiss::render_resource_kind::texture}};
  gneiss::asset_batch_request assets_request;
  check(gneiss::test_support::take_value(app->load_assets(uris, false), assets_request) ==
        gneiss::error::success);
  check(!empty && !empty.progress());
  check(!empty.take() && empty.take().error() == gneiss::error::invalid_state);
  gneiss::asset_load_completion completion;
  bool ready{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready) {
    if (mode == gneiss::tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    // 普通主循环能够独立推进通用资产批次。
    check(app->run(1).ok());
    check(gneiss::test_support::receive_request(assets_request, completion, ready) ==
          gneiss::error::success);
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  check(completion.result == gneiss::error::success && completion.request != 0 &&
        completion.assets.size() == 1U &&
        gneiss::resource_kind(completion.assets.front()) ==
            gneiss::render_resource_kind::texture);
  const auto wait = [&] {
    bool completed = false;
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed) {
      if (mode == gneiss::tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(app->run(1).ok());
      check(gneiss::test_support::receive_request(assets_request, completion, completed) ==
            gneiss::error::success);
      check(std::chrono::steady_clock::now() < limit);
      std::this_thread::yield();
    }
  };
  std::ofstream(files.root / "m.mesh")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})";
  const auto write_material = [&](bool green) {
    std::ofstream(files.root / "m.material")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":)"
        << (green ? "[0,1,0,1]" : "[1,0,0,1]")
        << R"(},"texture_uris":["asset://a.texture.json","","","",""]})";
  };
  write_material(false);
  std::ofstream(files.root / "s.scene")
      << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://m.mesh","material":"asset://m.material","uuid":"156c772a-1a1d-4eae-83df-5de0465c4534"}}]},{"uuid":"00000000-0000-4000-8000-000000000003","parent":{"object":null,"source":null},"transform":{"translation":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://m.mesh","material":"asset://m.material","uuid":"15e8daeb-ec91-4d5f-98b9-095db06f7148"}}]}]})";
  constexpr std::string_view scene_uri = "asset://s.scene";
  gneiss::scene_instance scene{};
  check(gneiss::error{gneiss::test_support::load_scene(
            *app, scene_uri, &scene, &scheduler)} == gneiss::error::success);
  const auto capture = [&](std::uint64_t index) {
    gneiss::test_support::scene_node_info info;
    check(gneiss::error{gneiss::test_support::take_value(
              gneiss::test_support::scene_access{*app}->get_node_info(scene, index), info)} ==
          gneiss::error::success);
    const auto object = app->native_world()->find(info.node);
    const auto* component = object.try_component<gneiss::mesh_component>();
    check(component != nullptr);
    return *component;
  };
  const auto initial = capture(0);
  check(initial.material == capture(1).material);
  write_material(true);
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  wait();
  // 只请求纹理不重读材质，场景使用者保持稳定资源引用。
  check(completion.result == gneiss::error::success && completion.assets.size() == 1U);
  const gneiss::render_resource_source material_source{
      .uri = "asset://m.material", .type = gneiss::render_resource_kind::material};
  gneiss::render_resource_ref selected_before;
  check(
      gneiss::test_support::take_value(app->assets().find(material_source, app->assets().profile()),
                                       selected_before) == gneiss::error::success);
  // 缓存命中立即返回；混合请求只加载缺失项，缓存资源仍保持同一引用。
  {
    auto cached = app->load_assets({&material_source, 1});
    check(cached && cached->ready());
    auto cached_value = cached->take();
    check(cached_value && cached_value->assets.size() == 1 &&
          cached_value->assets.front() == selected_before);
    std::ofstream(files.root / "extra.material")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["","","","",""]})";
    const std::array mixed_sources{gneiss::render_resource_source{
                                       .uri = "asset://extra.material",
                                       .type = gneiss::render_resource_kind::material},
                                   material_source};
    auto mixed = app->load_assets(mixed_sources);
    check(mixed && !mixed->ready());
    auto blocked = app->load_assets(mixed_sources);
    check(!blocked && blocked.error() == gneiss::error::not_ready);
    assets_request = std::move(*mixed);
    wait();
    check(completion.result.ok() && completion.assets.size() == 2 &&
          completion.results.size() == 2 && completion.assets.front() == selected_before);
    check(completion.results[0].applied && completion.results[1].applied);
  }
  write_material(false);
  auto preparation = app->load_assets({&material_source, 1}, true);
  check(bool(preparation));
  assets_request = std::move(*preparation);
  wait();
  check(completion.result == gneiss::error::success);
  gneiss::render_resource_ref selected_after;
  check(
      gneiss::test_support::take_value(app->assets().find(material_source, app->assets().profile()),
                                       selected_after) == gneiss::error::success &&
      selected_before == selected_after);
  // 恢复后续重载用例的输入；这里不修改场景使用者。
  write_material(true);
  const auto updated = capture(0);
  check(updated.material == capture(1).material && updated.material == initial.material);
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  wait();
  check(completion.result == gneiss::error::success);
  // 只改变纹理负载不替换材质身份；两个使用者继续持有同一资源。
  check(gneiss::test_support::take_codec(
            gneiss::encode_texture_ktx2(
                {.transfer = gneiss::texture_transfer::srgb,
                 .levels = {{.width = 1U,
                             .height = 1U,
                             .pixels = {std::byte{0}, std::byte{255}, std::byte{0},
                                        std::byte{255}}}}}),
            bytes, diagnostic) == std::nullopt);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  wait();
  check(completion.result == gneiss::error::success);
  const auto textured = capture(0);
  check(textured.material == capture(1).material && textured.material == updated.material);
  std::ofstream(files.root / "m.material") << "broken";
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  wait();
  check(completion.result == gneiss::error::success && completion.assets.size() == 1U);
  check(capture(0).material == textured.material && capture(1).material == textured.material);
  write_material(false);
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  check(assets_request.cancel().ok());
  wait();
  check(completion.state == gneiss::asset_load_state::cancelled &&
        completion.assets.empty());
  // 上传期间 ECS 改绑不妨碍资源更新，也不能被重载覆盖。
  gneiss::test_support::scene_node_info node;
  check(gneiss::error{gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*app}->get_node_info(scene, 0U), node)} ==
        gneiss::error::success);
  const auto world = app->native_world();
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  gneiss::mesh_component external;
  const std::array<gneiss::mesh_vertex, 3> vertices{
      {{.position = {0, 0, 0}}, {.position = {1, 0, 0}}, {.position = {0, 1, 0}}}};
  gneiss::mesh_view view{};
  view.vertices = vertices;
  check(gneiss::test_support::take_value(app->assets().create(view), external.mesh).ok());
  check(gneiss::test_support::take_value(app->assets().create({}, {}), external.material).ok());
  auto* external_target =
      app->native_world()->find(node.entity).try_component<gneiss::mesh_component>();
  check(external_target != nullptr);
  *external_target = external;
  wait();
  check(completion.result == gneiss::error::success &&
        completion.state == gneiss::asset_load_state::completed &&
        completion.assets.size() == 1U);
  auto unchanged = world->find(node.entity).try_component<gneiss::mesh_component>();
  check(unchanged && unchanged->mesh == external.mesh && unchanged->material == external.material);
  // 卸载冲突使用者后可重试；完成后释放租约，Application 仍可正常关闭。
  check(gneiss::error{gneiss::test_support::scene_access{*app}->unload(scene)} ==
        gneiss::error::success);
  check(gneiss::error{gneiss::test_support::load_scene(
            *app, scene_uri, &scene, &scheduler)} == gneiss::error::success);
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  wait();
  check(completion.result == gneiss::error::success);
  completion = {}; // 所有租约必须先于 Application 销毁。
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request) ==
        gneiss::error::success);
  check(assets_request.cancel().ok());
  auto discarded = std::move(assets_request);
  discarded.reset();
  check(gneiss::test_support::take_value(app->load_assets(uris, true), assets_request).ok());
  check(!discarded && discarded.cancel() == gneiss::error::invalid_state);
  wait();
  check(completion.request != 0 && completion.result.ok());
  // app 析构完成作用域回收；注入的宿主池仍然有效。
}
}
int main() try {
  run(gneiss::tasks::execution_mode::cooperative);
  run(gneiss::tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
