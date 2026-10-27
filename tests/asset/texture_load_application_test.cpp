// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "runtime/application.hpp"

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
void run(gneiss::engine::execution_mode mode) {
  using namespace gneiss;
  fixture files;
  std::ofstream(files.root / "a.texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::engine::encode_texture_ktx2(
            {.transfer = gneiss::engine::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = std::vector<std::byte>(4U, std::byte{255})}}},
            bytes, diagnostic) == gneiss::engine::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.asset_root = root;
  check(runtime::application::create(desc, app) == gneiss::engine::result::success);
  // 原生取消入口在无服务和无在途请求时返回未就绪。
  check(app->cancel_assets() == gneiss::engine::result::not_ready);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  check(app->cancel_assets() == gneiss::engine::result::not_ready);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::invalid_state);
  const std::array uris{gneiss::engine::render_asset_reload{
      .uri = "asset://a.texture.json", .type = gneiss::engine::render_asset_type::texture}};
  std::uint64_t request{};
  check(app->request_assets(uris, 2U, 1U, request, false) == gneiss::engine::result::success);
  gneiss::engine::texture_load_completion completion;
  bool ready{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready) {
    if (mode == gneiss::engine::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(app->poll_assets(completion, ready) == gneiss::engine::result::success);
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  check(completion.result == gneiss::engine::result::success && completion.request == request &&
        completion.assets.size() == 1U &&
        gneiss::engine::asset_kind(completion.assets.front()) ==
            gneiss::engine::render_asset_type::texture);
  const auto wait = [&] {
    bool completed = false;
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed) {
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(app->poll_assets(completion, completed) == gneiss::engine::result::success);
      check(std::chrono::steady_clock::now() < limit);
      std::this_thread::yield();
    }
  };
  std::ofstream(files.root / "m.mesh")
      << R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  const auto write_material = [&](bool green) {
    std::ofstream(files.root / "m.material")
        << R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":)"
        << (green ? "[0,1,0,1]" : "[1,0,0,1]")
        << R"(,"base_color_texture":"asset://a.texture.json"})";
  };
  write_material(false);
  std::ofstream(files.root / "s.scene")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}},{"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}}],"prefab_instances":[]})";
  constexpr std::string_view scene_uri = "asset://s.scene";
  gneiss::scene_instance_id scene{};
  check(gneiss::engine::result{gneiss::test_support::load_scene(
            *app, scene_uri, &scene, &scheduler)} == gneiss::engine::result::success);
  const auto capture = [&](std::uint64_t index) {
    gneiss::engine::scene_node_view info;
    check(gneiss::engine::result{app->scenes()->get_node_info(scene, index, info)} ==
          gneiss::engine::result::success);
    gneiss::engine::scene_instances::mesh_binding value;
    check(app->capture_mesh_binding(scene, info.node, value) == gneiss::engine::result::success);
    return value;
  };
  const auto initial = capture(0);
  check(initial.material == capture(1).material);
  write_material(true);
  check(app->request_assets(uris, 2U, 3U, request, true) == gneiss::engine::result::success);
  wait();
  // 只请求纹理不重读材质，场景使用者保持稳定资源引用。
  check(completion.result == gneiss::engine::result::success && completion.assets.size() == 1U);
  const gneiss::engine::render_asset_reload material_source{
      .uri = "asset://m.material", .type = gneiss::engine::render_asset_type::material};
  gneiss::engine::render_asset_ref selected_before;
  check(app->query_ready_asset(material_source, selected_before) ==
        gneiss::engine::result::success);
  write_material(false);
  check(app->prepare_edit_assets({&material_source, 1}, 31U, request) ==
        gneiss::engine::result::success);
  wait();
  check(completion.result == gneiss::engine::result::success);
  gneiss::engine::render_asset_ref selected_after;
  check(app->query_ready_asset(material_source, selected_after) ==
            gneiss::engine::result::success &&
        selected_before == selected_after);
  // 恢复后续重载用例的输入；这里不修改场景使用者。
  write_material(true);
  const auto updated = capture(0);
  check(updated.material == capture(1).material && updated.material == initial.material);
  check(app->request_assets(uris, 2U, 4U, request, true) == gneiss::engine::result::success);
  wait();
  check(completion.result == gneiss::engine::result::success);
  // 只改变纹理负载不替换材质身份；两个使用者继续持有同一资源。
  check(gneiss::engine::encode_texture_ktx2(
            {.transfer = gneiss::engine::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = {std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}}}}},
            bytes, diagnostic) == gneiss::engine::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  check(app->request_assets(uris, 2U, 40U, request, true) == gneiss::engine::result::success);
  wait();
  check(completion.result == gneiss::engine::result::success);
  const auto textured = capture(0);
  check(textured.material == capture(1).material && textured.material == updated.material);
  std::ofstream(files.root / "m.material") << "broken";
  check(app->request_assets(uris, 2U, 5U, request, true) == gneiss::engine::result::success);
  wait();
  check(completion.result == gneiss::engine::result::success && completion.assets.size() == 1U);
  check(capture(0).material == textured.material && capture(1).material == textured.material);
  write_material(false);
  check(app->request_assets(uris, 2U, 6U, request, true) == gneiss::engine::result::success);
  check(app->cancel_assets() == gneiss::engine::result::success);
  wait();
  check(completion.state == gneiss::engine::texture_load_state::cancelled &&
        completion.assets.empty());
  // 上传期间 ECS 改绑不妨碍资源更新，也不能被重载覆盖。
  gneiss::engine::scene_node_view node;
  check(gneiss::engine::result{app->scenes()->get_node_info(scene, 0U, node)} ==
        gneiss::engine::result::success);
  const auto world = app->native_world();
  check(app->request_assets(uris, 2U, 7U, request, true) == gneiss::engine::result::success);
  gneiss::engine::rid external_mesh{}, external_material{};
  check(app->resources().reserve_mesh(&external_mesh).ok());
  check(app->resources().reserve_material(&external_material).ok());
  const gneiss::engine::mesh_renderer external{
      app->resources().reference_mesh(external_mesh),
      app->resources().reference_material(external_material)};
  check(gneiss::engine::result{app->native_world()->entity_set_mesh_renderer(
            node.entity, external)} == gneiss::engine::result::success);
  wait();
  check(completion.result == gneiss::engine::result::success &&
        completion.state == gneiss::engine::texture_load_state::applied &&
        completion.assets.size() == 1U);
  const std::array unchanged{
      gneiss::engine::mesh_renderer_replacement{.entity = node.entity,
                                                .expected = {external.mesh, external.material},
                                                .replacement = {external.mesh, external.material}}};
  check(world->replace_mesh_renderers(unchanged) == gneiss::engine::result::success);
  // 卸载冲突使用者后可重试；完成后释放租约，Application 仍可正常关闭。
  check(gneiss::engine::result{app->scenes()->unload(scene)} == gneiss::engine::result::success);
  check(gneiss::engine::result{gneiss::test_support::load_scene(
            *app, scene_uri, &scene, &scheduler)} == gneiss::engine::result::success);
  check(app->request_assets(uris, 2U, 8U, request, true) == gneiss::engine::result::success);
  wait();
  check(completion.result == gneiss::engine::result::success);
  completion = {}; // 所有租约必须先于 Application 销毁。
  check(app->request_assets(uris, 2U, 2U, request, true) == gneiss::engine::result::success);
  check(app->cancel_assets() == gneiss::engine::result::success);
  // app 析构完成取消与作用域回收；注入的宿主池仍然有效。
}
}
int main() try {
  run(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
