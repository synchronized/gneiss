// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/texture_ktx2.hpp"
#include "engine/function/application/application_asset_reload_internal.hpp"
#include "engine/function/world/world_service.hpp"

#include <gneiss/engine/application.hpp>

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
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(asset_internal::encode_texture_ktx2(
            {.transfer = asset_internal::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = std::vector<std::byte>(4U, std::byte{255})}}},
            bytes, diagnostic) == asset_internal::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  sdk::application app;
  const auto root = files.root.string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(sdk::application::create_native(desc, app) == result::success);
  // 旧取消入口在无服务和无在途请求时保持幂等成功。
  check(application_internal::cancel_textures(app.get()) == GNEISS_SUCCESS);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  check(application_internal::cancel_textures(app.get()) == GNEISS_SUCCESS);
  check(application_internal::attach_task_executor(app.get(), scheduler) ==
        GNEISS_ERROR_INVALID_STATE);
  const std::vector<std::string> uris{"asset://a.texture.json"};
  std::uint64_t request{};
  check(application_internal::request_textures(app.get(), uris, 2U, 1U, request, false) ==
        GNEISS_SUCCESS);
  render_internal::texture_load_completion completion;
  bool ready{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready) {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(application_internal::poll_textures(app.get(), completion, ready) == GNEISS_SUCCESS);
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  check(completion.result == GNEISS_SUCCESS && completion.request == request &&
        completion.textures.size() == 1U);
  const auto wait = [&] {
    bool completed = false;
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed) {
      if (mode == tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(application_internal::poll_textures(app.get(), completion, completed) ==
            GNEISS_SUCCESS);
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
  check(gneiss_scene_instance_load(app.get(), scene_uri.data(), scene_uri.size(), &scene) ==
        GNEISS_SUCCESS);
  const auto capture = [&](std::uint64_t index) {
    gneiss_scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
    check(gneiss_scene_instance_get_node_info(app.get(), scene, index, &info) == GNEISS_SUCCESS);
    application_internal::mesh_binding value;
    check(application_internal::capture_mesh_binding(app.get(), scene, info.node, value) ==
          GNEISS_SUCCESS);
    return value;
  };
  const auto initial = capture(0);
  check(initial.material == capture(1).material);
  write_material(true);
  check(application_internal::request_textures(app.get(), uris, 2U, 3U, request) == GNEISS_SUCCESS);
  wait();
  // 仅请求纹理也必须重新准备引用它的材质，并返回固定版本回执。
  check(completion.result == GNEISS_SUCCESS && completion.assets.size() == 2U &&
        completion.versions.size() == 2U);
  const auto updated = capture(0);
  check(updated.material == capture(1).material && updated.material != initial.material);
  const auto versions = completion.versions;
  check(application_internal::request_textures(app.get(), uris, 2U, 4U, request) == GNEISS_SUCCESS);
  wait();
  check(completion.result == GNEISS_SUCCESS && completion.versions == versions);
  // 只改变纹理负载，材质文档字节不变；两个使用者仍须切换到同一新材质产物。
  check(asset_internal::encode_texture_ktx2(
            {.transfer = asset_internal::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = {std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}}}}},
            bytes, diagnostic) == asset_internal::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  check(application_internal::request_textures(app.get(), uris, 2U, 40U, request) ==
        GNEISS_SUCCESS);
  wait();
  check(completion.result == GNEISS_SUCCESS && completion.versions != versions);
  const auto textured = capture(0);
  check(textured.material == capture(1).material && textured.material != updated.material);
  std::ofstream(files.root / "m.material") << "broken";
  check(application_internal::request_textures(app.get(), uris, 2U, 5U, request) == GNEISS_SUCCESS);
  wait();
  check(completion.result != GNEISS_SUCCESS && completion.assets.empty() &&
        completion.versions.empty());
  check(capture(0).material == textured.material && capture(1).material == textured.material);
  write_material(false);
  check(application_internal::request_textures(app.get(), uris, 2U, 6U, request) == GNEISS_SUCCESS);
  check(application_internal::cancel_render_assets(app.get()) == GNEISS_SUCCESS);
  wait();
  check(completion.state == render_internal::texture_load_state::cancelled &&
        completion.assets.empty());
  // 上传期间裸 ECS 使用者改变组件，整批场景切换须拒绝，不能覆盖该修改。
  gneiss_scene_instance_node_info node = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  check(gneiss_scene_instance_get_node_info(app.get(), scene, 0U, &node) == GNEISS_SUCCESS);
  gneiss_world world{};
  check(gneiss_application_get_world(app.get(), &world) == GNEISS_SUCCESS);
  check(application_internal::request_textures(app.get(), uris, 2U, 7U, request) == GNEISS_SUCCESS);
  const gneiss_mesh_renderer external{UINT64_MAX, UINT64_MAX};
  check(gneiss_world_entity_set_mesh_renderer(world, node.entity, &external) == GNEISS_SUCCESS);
  wait();
  check(completion.result == GNEISS_ERROR_INVALID_STATE &&
        completion.state == render_internal::texture_load_state::failed &&
        completion.assets.empty() && completion.textures.empty() && completion.versions.empty());
  const std::array unchanged{
      world_internal::mesh_renderer_replacement{.entity = gneiss::entity_id{node.entity},
                                                .expected = {external.mesh, external.material},
                                                .replacement = {external.mesh, external.material}}};
  check(world_internal::replace_mesh_renderers(world, unchanged) == GNEISS_SUCCESS);
  // 卸载冲突使用者后可重试；完成后释放租约，Application 仍可正常关闭。
  check(gneiss_scene_instance_unload(app.get(), scene) == GNEISS_SUCCESS);
  check(gneiss_scene_instance_load(app.get(), scene_uri.data(), scene_uri.size(), &scene) ==
        GNEISS_SUCCESS);
  check(application_internal::request_textures(app.get(), uris, 2U, 8U, request) == GNEISS_SUCCESS);
  wait();
  check(completion.result == GNEISS_SUCCESS && completion.versions.size() == 2U);
  completion = {}; // 所有租约必须先于 Application 销毁。
  check(application_internal::request_textures(app.get(), uris, 2U, 2U, request) == GNEISS_SUCCESS);
  check(application_internal::cancel_textures(app.get()) == GNEISS_SUCCESS);
  // app 析构完成取消与作用域回收；注入的宿主池仍然有效。
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
