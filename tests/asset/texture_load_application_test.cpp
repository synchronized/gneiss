// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/c_owner.hpp"
#include "engine/core/result.hpp"
#include "engine/function/world/world_service.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"
#include "runtime/application_asset_reload_internal.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/scene.hpp>

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
void run(gneiss::engine::core::tasks::execution_mode mode) {
  using namespace gneiss;
  fixture files;
  std::ofstream(files.root / "a.texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(engine::resource::encode_texture_ktx2(
            {.transfer = engine::resource::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = std::vector<std::byte>(4U, std::byte{255})}}},
            bytes, diagnostic) == engine::resource::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  engine::core::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  const auto root = files.root.string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::engine::core::result{gneiss_application_create(&desc, app.put())} ==
        gneiss::engine::core::result::success);
  // 旧取消入口在无服务和无在途请求时保持幂等成功。
  check(runtime::cancel_textures(app.get()) == gneiss::engine::core::result::success);
  check(runtime::attach_task_executor(app.get(), scheduler) ==
        gneiss::engine::core::result::success);
  check(runtime::cancel_textures(app.get()) == gneiss::engine::core::result::success);
  check(runtime::attach_task_executor(app.get(), scheduler) ==
        gneiss::engine::core::result::invalid_state);
  const std::vector<std::string> uris{"asset://a.texture.json"};
  std::uint64_t request{};
  check(runtime::request_textures(app.get(), uris, 2U, 1U, request, false) ==
        gneiss::engine::core::result::success);
  engine::render::texture_load_completion completion;
  bool ready{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready) {
    if (mode == engine::core::tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(runtime::poll_textures(app.get(), completion, ready) ==
          gneiss::engine::core::result::success);
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  check(completion.result == gneiss::engine::core::result::success &&
        completion.request == request && completion.assets.size() == 1U &&
        completion.assets.front().type() == gneiss::engine::render::render_asset_type::texture);
  const auto wait = [&] {
    bool completed = false;
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed) {
      if (mode == engine::core::tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(runtime::poll_textures(app.get(), completion, completed) ==
            gneiss::engine::core::result::success);
      check(std::chrono::steady_clock::now() < limit);
      std::this_thread::yield();
    }
  };
  std::ofstream(files.root / "m.mesh")
      << R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  const auto write_material = [&](bool green) {
    std::ofstream(files.root / "m.material")
        << R"({"format":"gneiss.material","version":2,"color":)"
        << (green ? "[0,1,0,1]" : "[1,0,0,1]")
        << R"(,"base_color_texture":"asset://a.texture.json"})";
  };
  write_material(false);
  std::ofstream(files.root / "s.scene")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}},{"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}}],"prefab_instances":[]})";
  constexpr std::string_view scene_uri = "asset://s.scene";
  gneiss_scene_instance scene{};
  check(gneiss::engine::core::result{
            gneiss_scene_instance_load(app.get(), scene_uri.data(), scene_uri.size(), &scene)} ==
        gneiss::engine::core::result::success);
  const auto capture = [&](std::uint64_t index) {
    gneiss_scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
    check(gneiss::engine::core::result{gneiss_scene_instance_get_node_info(
              app.get(), scene, index, &info)} == gneiss::engine::core::result::success);
    runtime::mesh_binding value;
    check(runtime::capture_mesh_binding(app.get(), scene, info.node, value) ==
          gneiss::engine::core::result::success);
    return value;
  };
  const auto initial = capture(0);
  check(initial.material == capture(1).material);
  write_material(true);
  check(runtime::request_textures(app.get(), uris, 2U, 3U, request) ==
        gneiss::engine::core::result::success);
  wait();
  // 只请求纹理不重读材质，场景使用者保持稳定资源引用。
  check(completion.result == gneiss::engine::core::result::success &&
        completion.assets.size() == 1U);
  const engine::render::render_asset_reload material_source{
      .uri = "asset://m.material", .type = engine::render::render_asset_type::material};
  engine::render::render_asset_lease selected_before;
  check(runtime::query_ready_asset(app.get(), material_source, selected_before) ==
        gneiss::engine::core::result::success);
  write_material(false);
  check(runtime::prepare_edit_assets(app.get(), {&material_source, 1}, 31U, request) ==
        gneiss::engine::core::result::success);
  wait();
  check(completion.result == gneiss::engine::core::result::success);
  engine::render::render_asset_lease selected_after;
  check(runtime::query_ready_asset(app.get(), material_source, selected_after) ==
            gneiss::engine::core::result::success &&
        selected_before == selected_after);
  // 恢复后续重载用例的输入；这里不修改场景使用者。
  write_material(true);
  const auto updated = capture(0);
  check(updated.material == capture(1).material && updated.material == initial.material);
  check(runtime::request_textures(app.get(), uris, 2U, 4U, request) ==
        gneiss::engine::core::result::success);
  wait();
  check(completion.result == gneiss::engine::core::result::success);
  // 只改变纹理负载不替换材质身份；两个使用者继续持有同一资源。
  check(engine::resource::encode_texture_ktx2(
            {.transfer = engine::resource::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = {std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}}}}},
            bytes, diagnostic) == engine::resource::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  check(runtime::request_textures(app.get(), uris, 2U, 40U, request) ==
        gneiss::engine::core::result::success);
  wait();
  check(completion.result == gneiss::engine::core::result::success);
  const auto textured = capture(0);
  check(textured.material == capture(1).material && textured.material == updated.material);
  std::ofstream(files.root / "m.material") << "broken";
  check(runtime::request_textures(app.get(), uris, 2U, 5U, request) ==
        gneiss::engine::core::result::success);
  wait();
  check(completion.result == gneiss::engine::core::result::success &&
        completion.assets.size() == 1U);
  check(capture(0).material == textured.material && capture(1).material == textured.material);
  write_material(false);
  check(runtime::request_textures(app.get(), uris, 2U, 6U, request) ==
        gneiss::engine::core::result::success);
  check(runtime::cancel_render_assets(app.get()) == gneiss::engine::core::result::success);
  wait();
  check(completion.state == engine::render::texture_load_state::cancelled &&
        completion.assets.empty());
  // 上传期间 ECS 改绑不妨碍资源更新，也不能被重载覆盖。
  gneiss_scene_instance_node_info node = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  check(gneiss::engine::core::result{gneiss_scene_instance_get_node_info(
            app.get(), scene, 0U, &node)} == gneiss::engine::core::result::success);
  gneiss_world world{};
  check(gneiss::engine::core::result{gneiss_application_get_world(app.get(), &world)} ==
        gneiss::engine::core::result::success);
  check(runtime::request_textures(app.get(), uris, 2U, 7U, request) ==
        gneiss::engine::core::result::success);
  const gneiss_mesh_renderer external{UINT64_MAX, UINT64_MAX};
  check(gneiss::engine::core::result{gneiss_world_entity_set_mesh_renderer(
            world, node.entity, &external)} == gneiss::engine::core::result::success);
  wait();
  check(completion.result == gneiss::engine::core::result::success &&
        completion.state == engine::render::texture_load_state::applied &&
        completion.assets.size() == 1U);
  const std::array unchanged{
      engine::world::mesh_renderer_replacement{.entity = gneiss::entity_id{node.entity},
                                               .expected = {external.mesh, external.material},
                                               .replacement = {external.mesh, external.material}}};
  check(engine::world::replace_mesh_renderers(world, unchanged) ==
        gneiss::engine::core::result::success);
  // 卸载冲突使用者后可重试；完成后释放租约，Application 仍可正常关闭。
  check(gneiss::engine::core::result{gneiss_scene_instance_unload(app.get(), scene)} ==
        gneiss::engine::core::result::success);
  check(gneiss::engine::core::result{
            gneiss_scene_instance_load(app.get(), scene_uri.data(), scene_uri.size(), &scene)} ==
        gneiss::engine::core::result::success);
  check(runtime::request_textures(app.get(), uris, 2U, 8U, request) ==
        gneiss::engine::core::result::success);
  wait();
  check(completion.result == gneiss::engine::core::result::success);
  completion = {}; // 所有租约必须先于 Application 销毁。
  check(runtime::request_textures(app.get(), uris, 2U, 2U, request) ==
        gneiss::engine::core::result::success);
  check(runtime::cancel_textures(app.get()) == gneiss::engine::core::result::success);
  // app 析构完成取消与作用域回收；注入的宿主池仍然有效。
}
}
int main() try {
  run(gneiss::engine::core::tasks::execution_mode::cooperative);
  run(gneiss::engine::core::tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
