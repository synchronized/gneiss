// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_frame_packet.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/texture/texture_binary.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include "runtime/application.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <granit/asset_tools/texture_builder.hpp>
#include <granit/renderer/texture_asset.hpp>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("像素验收失败，行=" + std::to_string(where.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-asset-pixel-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  explicit fixture(bool pbr = false) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "t.texture.json")
        << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
    std::ofstream(root / "m.material.json")
        << R"({"format":"gneiss.material","version":5,"color":[1,1,1,1],"base_color_texture":"asset://t.texture.json","metallic":0,"roughness":1})";
    if (pbr) {
      std::ofstream(root / "m.material.json")
          << R"({"format":"gneiss.material","version":5,"color":[0,0,0,1],"base_color_texture":null,"metallic":0,"roughness":1,"emissive":[1,1,1],"emissive_texture":"asset://t.texture.json"})";
    }
    std::ofstream(root / "g.mesh.json")
        << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.8,-0.7,0],[0.8,-0.7,0],[0,0.8,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
    std::ofstream(root / "s.scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"camera":{"vertical_field_of_view_radians":1.04719755,"near_plane":0.1,"far_plane":100,"is_primary":true}}},{"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://g.mesh.json","material":"asset://m.material.json"}}}],"prefab_instances":[]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  void color(std::byte red, std::byte blue) {
    std::vector<std::byte> bytes;
    std::string message;
    check(gneiss::engine::encode_texture_ktx2(
              {.transfer = gneiss::engine::texture_transfer::srgb,
               .levels = {{.width = 1U,
                           .height = 1U,
                           .pixels = {red, std::byte{20}, blue, std::byte{255}}}}},
              bytes, message) == gneiss::engine::texture_ktx2_result::success);
    std::ofstream stream(root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
};
void run_asset_reload() {
  fixture files(true);
  files.color(std::byte{255}, std::byte{0});
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler).ok());
  const auto reload = [&](std::span<const gneiss::engine::asset_source> requests) {
    std::uint64_t request{};
    auto status = app->request_assets(requests, 1, 1, request, true);
    if (status.failed())
      return status;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      (void)scheduler.run_ready();
      gneiss::engine::asset_load_completion completion;
      bool ready{};
      status = app->poll_assets(completion, ready);
      if (status.failed() || ready)
        return status.failed() ? status : completion.result;
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance_id scene{};
  check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
        gneiss::engine::result::success);
  const auto pixel = [&] {
    gneiss::engine::frame_image image;
    check(app->capture_frame(128U, 128U, image) == gneiss::engine::result::success);
    constexpr auto offset = ((64U * 128U) + 64U) * 4U;
    return std::array{
        std::to_integer<unsigned>(image.pixels[offset]),
        std::to_integer<unsigned>(image.pixels[offset + 2U]),
    };
  };
  const auto await_color = [&](bool blue) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto value = pixel();
    while (value[blue ? 1U : 0U] <= value[blue ? 0U : 1U] + 30U) {
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      value = pixel();
    }
    return value;
  };
  const auto initial = await_color(false);
  check(initial[0] > initial[1] + 30U);
  files.color(std::byte{0}, std::byte{255});
  const std::array requests{gneiss::engine::asset_source{
      .uri = "asset://t.texture.json", .type = gneiss::engine::asset_type::texture}};
  check(reload(requests) == gneiss::engine::result::success);
  // 不重载场景、不重建材质：已有引用必须看到同一纹理 RID 的新内容。
  const auto updated = await_color(true);
  check(updated[1] > updated[0] + 30U);
  std::ofstream(files.root / "t.texture.json") << "{";
  check(reload(requests) == gneiss::engine::result::invalid_argument);
  check(pixel() == updated);
}

void run_pending_resources() {
  fixture files(true);
  files.color(std::byte{255}, std::byte{0});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance_id scene{};
  check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene)} ==
        gneiss::engine::result::success);
  auto native = app;
  check(native != nullptr);
  auto& resources = native->render();
  auto& resources_factory = native->assets();
  gneiss::engine::mesh mesh_ref;
  gneiss::engine::texture texture_ref;
  check(resources_factory.reserve_mesh(mesh_ref) == gneiss::engine::result::success);
  check(resources_factory.reserve_texture(texture_ref) == gneiss::engine::result::success);
  const auto mesh = resource_handle(mesh_ref);
  const auto texture = resource_handle(texture_ref);
  gneiss::engine::material_data material_desc;
  material_desc.parameters.color[0] = material_desc.parameters.color[1] =
      material_desc.parameters.color[2] = 0.0F;
  material_desc.parameters.emissive[0] = material_desc.parameters.emissive[1] =
      material_desc.parameters.emissive[2] = 1.0F;
  material_desc.emissive_texture = texture;
  gneiss::engine::material material_ref;
  check(gneiss::engine::result{resources_factory.create_material(
            material_desc.parameters, {gneiss::engine::texture{}, {}, {}, {}, texture_ref},
            material_ref)} == gneiss::engine::result::success);
  gneiss::engine::scene_node_view info;
  check(gneiss::engine::result{app->scenes()->get_node_info(scene, 1U, info)} ==
        gneiss::engine::result::success);
  check(gneiss::has_flags(info.component_flags, gneiss::scene_node_components::mesh_renderer));
  const gneiss::engine::mesh_renderer binding{.mesh = resources_factory.find_mesh(mesh),
                                              .material = material_ref};
  check(gneiss::engine::result{native->native_world()->entity_set_mesh_renderer(
            info.entity, binding)} == gneiss::engine::result::success);
  const auto pixel = [&] {
    gneiss::engine::frame_image image;
    check(app->capture_frame(128U, 128U, image) == gneiss::engine::result::success);
    constexpr auto offset = ((64U * 128U) + 64U) * 4U;
    return std::array{
        std::to_integer<unsigned>(image.pixels[offset]),
        std::to_integer<unsigned>(image.pixels[offset + 1U]),
        std::to_integer<unsigned>(image.pixels[offset + 2U]),
    };
  };
  const auto hidden = pixel();
  auto geometry = std::make_shared<gneiss::engine::mesh_data>();
  geometry->vertices = {
      {.position = {-0.8F, -0.7F, 0.0F}, .uv = {0.0F, 0.0F}},
      {.position = {0.8F, -0.7F, 0.0F}, .uv = {1.0F, 0.0F}},
      {.position = {0.0F, 0.8F, 0.0F}, .uv = {0.5F, 1.0F}},
  };
  geometry->normals.assign(3U, {0.0F, 0.0F, 1.0F});
  check(resources.replace_mesh(mesh, geometry));
  const auto placeholder = pixel();
  check(placeholder[0] > hidden[0] + 30U && placeholder[1] > hidden[1] + 30U &&
        placeholder[2] > hidden[2] + 30U);
  auto red = std::make_shared<gneiss::engine::texture_data>();
  red->width = red->height = 1U;
  red->format = gneiss::engine::texture_format::rgba8_unorm;
  red->color_space = gneiss::engine::texture_color_space::srgb;
  red->levels = {
      {
          .width = 1U,
          .height = 1U,
          .pixels = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}},
      },
  };
  check(resources.replace_texture(texture, red));
  const auto await_pixel = [&](bool is_blue) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto color = pixel();
    const auto expected = is_blue ? 2U : 0U;
    const auto other = is_blue ? 0U : 2U;
    while (!(color[expected] > color[1] + 30U && color[expected] > color[other] + 30U) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      color = pixel();
    }
    return color;
  };
  const auto ready = await_pixel(false);
  check(ready[0] > ready[1] + 30U && ready[0] > ready[2] + 30U);
  auto blue = std::make_shared<gneiss::engine::texture_data>(*red);
  blue->levels.front().pixels = {std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
  check(resources.replace_texture(texture, blue));
  const auto updated = await_pixel(true);
  check(updated[2] > updated[0] + 30U && updated[2] > updated[1] + 30U);
  // 无效完成不能覆盖旧内容；已缓存 GPU 镜像也不能掩盖 RID 销毁。
  check(!resources.replace_texture(texture, {}));
  check(!resources.replace_mesh(mesh, {}));
  check(pixel() == updated);
  check(gneiss::engine::result{resources.destroy_texture(texture)} ==
        gneiss::engine::result::success);
  gneiss::engine::frame_image invalid;
  check(app->capture_frame(128U, 128U, invalid) == gneiss::engine::result::invalid_handle);
  check(!resources.replace_texture(texture, red));
  check(gneiss::engine::result{native->native_world()->entity_remove_mesh_renderer(info.entity)} ==
        gneiss::engine::result::success);
  check(pixel() == hidden);
  check(gneiss::engine::result{resources.destroy_material(resource_handle(material_ref))} ==
        gneiss::engine::result::success);
  check(gneiss::engine::result{resources.destroy_mesh(mesh)} == gneiss::engine::result::success);
}

void run_mip_sampling(bool packaged = false, bool async = false) {
  fixture files(true);
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.8,-0.7,0],[0.8,-0.7,0],[0,0.8,0]],"uvs":[[0,0],[1024,0],[512,1024]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  gneiss::engine::texture_ktx2 texture;
  texture.transfer = gneiss::engine::texture_transfer::srgb;
  for (std::uint32_t width = 8U; width > 0U; width /= 2U) {
    gneiss::engine::texture_mip mip{.width = width, .height = width, .pixels = {}};
    for (std::uint32_t pixel = 0; pixel < width * width; ++pixel) {
      mip.pixels.insert(mip.pixels.end(),
                        {width == 8U ? std::byte{0} : std::byte{255}, std::byte{0},
                         width == 8U ? std::byte{255} : std::byte{0}, std::byte{255}});
    }
    texture.levels.push_back(std::move(mip));
  }
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::engine::encode_texture_ktx2(texture, bytes, diagnostic) ==
        gneiss::engine::texture_ktx2_result::success);
  if (packaged) {
    std::vector<std::byte> payload;
    std::vector<granit::asset_tools::texture::subresource_info> subresources;
    for (std::uint32_t mip = 0; mip < texture.levels.size(); ++mip) {
      const auto& level = texture.levels[mip];
      subresources.push_back({.mip_level = mip,
                              .array_layer = 0U,
                              .data_offset = payload.size(),
                              .data_size = level.pixels.size(),
                              .bytes_per_row = level.width * 4U,
                              .rows_per_image = level.height});
      payload.insert(payload.end(), level.pixels.begin(), level.pixels.end());
    }
    auto unused = payload;
    for (auto& value : unused) {
      value = std::byte{};
    }
    // 第一个变体不允许采样，必须选择非零偏移的第二个 RGBA8 变体。
    const std::array variants{
        granit::asset_tools::texture::variant_desc{.format = granit::texture_format::rgba8_srgb,
                                                   .usage =
                                                       granit::texture_usage::transfer_destination,
                                                   .payload = unused,
                                                   .subresources = subresources},
        granit::asset_tools::texture::variant_desc{
            .format = granit::texture_format::rgba8_srgb,
            .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
            .payload = payload,
            .subresources = subresources},
    };
    const auto [status, built] = granit::asset_tools::texture::build(
        {.dimension = granit::texture_dimension::two_dimensional,
         .width = 8U,
         .height = 8U,
         .depth = 1U,
         .array_layers = 1U,
         .mip_levels = static_cast<std::uint32_t>(texture.levels.size()),
         .variants = variants});
    check(status == granit::result::success);
    granit::texture_asset_info info;
    check(granit::inspect_texture_asset(built.manifest(), info) == granit::result::success);
    check(info.variants.size() == 2U && info.variants[1].payload_offset > 0U);
    check(gneiss::engine::encode_texture_binary(built.manifest(), built.payload(), bytes,
                                                diagnostic) ==
          gneiss::engine::texture_binary_result::success);
    std::ofstream(files.root / "t.texture.json")
        << R"({"format":"gneiss.texture","version":2,"source":"asset://image.gneiss-texture","color_space":"srgb","usage":"default"})";
  }
  {
    std::ofstream stream(files.root / (packaged ? "image.gneiss-texture" : "image.ktx2"),
                         std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  gneiss::engine::task_scheduler scheduler({.workers = 1U});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance_id scene{};
  if (async) {
    using namespace runtime;
    check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
    std::uint64_t request{};
    check(app->request_scene(uri, 1U, 1U, request) == gneiss::engine::result::success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (true) {
      check(std::chrono::steady_clock::now() < deadline);
      scene_load_completion completion;
      bool terminal{};
      check(app->poll_scene(completion, terminal) == gneiss::engine::result::success);
      if (terminal)
        std::fprintf(stderr, "scene result %d: %s\n", completion.result.code(),
                     completion.message.c_str());
      check(!terminal);
      scene_load_progress progress;
      check(app->scene_progress(progress));
      if (progress.phase == scene_load_phase::ready) {
        check(app->activate_scene(request, completion) == gneiss::engine::result::success);
        check(completion.progress.texture_payload_bytes == 0U);
        auto native = app;
        gneiss::engine::frame_image initial;
        check(app->capture_frame(128U, 128U, initial) == gneiss::engine::result::success);
        while (native->visual_assets_loading()) {
          check(std::chrono::steady_clock::now() < deadline);
          native->advance_scene_load();
          check(gneiss::engine::result{app->run(1U)} == gneiss::engine::result::success);
          std::this_thread::yield();
        }
        gneiss::engine::frame_image loaded;
        check(app->capture_frame(128U, 128U, loaded) == gneiss::engine::result::success);
        check(initial.pixels != loaded.pixels);
        break;
      }
      std::this_thread::yield();
    }
  } else {
    check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
          gneiss::engine::result::success);
  }
  check(gneiss::engine::result{app->run(3U)} == gneiss::engine::result::success);
  gneiss::engine::frame_image image;
  check(app->capture_frame(128U, 128U, image) == gneiss::engine::result::success);
  const auto offset = (64U * 128U + 64U) * 4U;
  // LOD0 全蓝，缩小时应采样全红的较低分辨率层；阈值不依赖具体色调映射曲线。
  check(std::to_integer<unsigned>(image.pixels[offset]) >
        std::to_integer<unsigned>(image.pixels[offset + 2U]) + 30U);
}

std::array<unsigned, 3> material_pixel(std::string_view slot, std::array<std::byte, 4> pixel,
                                       bool reflected = false) {
  fixture files;
  std::ofstream(files.root / "t.texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"linear","usage":"default"})";
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":5,"color":[0.5,0.5,0.5,1],"base_color_texture":null,"metallic":1,"roughness":1,")"
      << slot << R"(":"asset://t.texture.json","emissive":[)"
      << (slot == "emissive_texture" ? "1,1,1" : "0,0,0") << "]}";
  gneiss::engine::mesh_binary_data mesh{.vertices = {{{-0.8F, -0.7F, 0.0F}, {0, 0}, {0, 0, 1}},
                                                     {{0.8F, -0.7F, 0.0F}, {1, 0}, {0, 0, 1}},
                                                     {{0.0F, 0.8F, 0.0F}, {0.5F, 1}, {0, 0, 1}}},
                                        .indices = {0U, 1U, 2U},
                                        .tangents = {{1, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}}};
  std::vector<std::byte> bytes;
  gneiss::engine::mesh_binary_diagnostic mesh_diagnostic;
  check(gneiss::engine::encode_mesh_binary(mesh, bytes, mesh_diagnostic) ==
        gneiss::engine::mesh_binary_result::success);
  {
    std::ofstream stream(files.root / "g.gneiss-mesh", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  {
    std::ifstream stream(files.root / "s.scene.json");
    std::string scene{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    stream.close();
    scene.replace(scene.find("g.mesh.json"), 11U, "g.gneiss-mesh");
    if (reflected) {
      constexpr std::string_view scale = "\"scale\":[1,1,1]";
      scene.replace(scene.rfind(scale), scale.size(), "\"scale\":[-1,1,1]");
    }
    std::ofstream(files.root / "s.scene.json") << scene;
  }
  std::string diagnostic;
  check(gneiss::engine::encode_texture_ktx2(
            {.transfer = gneiss::engine::texture_transfer::linear,
             .levels = {{.width = 1U, .height = 1U, .pixels = {pixel.begin(), pixel.end()}}}},
            bytes, diagnostic) == gneiss::engine::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance_id scene{};
  check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene)} ==
        gneiss::engine::result::success);
  check(gneiss::engine::result{app->run(3U)} == gneiss::engine::result::success);
  gneiss::engine::frame_image image;
  check(app->capture_frame(128U, 128U, image) == gneiss::engine::result::success);
  const auto offset = (64U * 128U + 64U) * 4U;
  return {std::to_integer<unsigned>(image.pixels[offset]),
          std::to_integer<unsigned>(image.pixels[offset + 1U]),
          std::to_integer<unsigned>(image.pixels[offset + 2U])};
}

struct state_case {
  std::string_view mode{"OPAQUE"};
  float alpha{1};
  float cutoff{0.5F};
  bool back{};
  bool double_sided{};
  std::array<float, 4> color{1, 1, 1, 1};
  unsigned uv_set{};
  unsigned wrap{};
  bool layers{};
  bool reverse_submission{};
  bool foreground{};
  bool receiver{};
};
std::array<unsigned, 3> state_pixel(const state_case& test,
                                    std::vector<std::byte>* frame = nullptr) {
  std::fprintf(
      stderr,
      "material-state mode=%.*s alpha=%g back=%d double=%d uv=%u wrap=%u layers=%d receiver=%d\n",
      static_cast<int>(test.mode.size()), test.mode.data(), static_cast<double>(test.alpha),
      test.back, test.double_sided, test.uv_set, test.wrap, test.layers, test.receiver);
  fixture files;
  std::ofstream material(files.root / "m.material.json");
  material
      << R"({"format":"gneiss.material","version":5,"color":[1,1,1,)" << test.alpha
      << R"(],"base_color_texture":"asset://t.texture.json","metallic":0,"roughness":1,"alpha_mode":")"
      << test.mode << R"(","alpha_cutoff":)" << test.cutoff << R"(,"double_sided":)"
      << (test.double_sided ? "true" : "false") << R"(,"sampling":[)";
  for (unsigned i = 0; i < 5; ++i) {
    material << (i == 0 ? "" : ",") << R"({"uv_set":)" << (i == 0 ? test.uv_set : 0)
             << R"(,"mag_filter":0,"min_filter":0,"mip_filter":0,"address_u":)" << test.wrap
             << R"(,"address_v":0})";
  }
  material << "]}";
  material.close();
  gneiss::engine::mesh_binary_data mesh;
  mesh.vertices = {{{-0.8F, -0.7F, 0}, {1.25F, 0.5F}, {0, 0, 1}},
                   {{0.8F, -0.7F, 0}, {1.25F, 0.5F}, {0, 0, 1}},
                   {{0, 0.8F, 0}, {1.25F, 0.5F}, {0, 0, 1}}};
  mesh.indices =
      test.back ? std::vector<std::uint32_t>{2, 1, 0} : std::vector<std::uint32_t>{0, 1, 2};
  mesh.tangents.assign(3, {1, 0, 0, 1});
  mesh.uv1.assign(3, {0.75F, 0.5F});
  mesh.colors.assign(3, {test.color[0], test.color[1], test.color[2], test.color[3]});
  std::vector<std::byte> bytes;
  gneiss::engine::mesh_binary_diagnostic diagnostic;
  check(gneiss::engine::encode_mesh_binary(mesh, bytes, diagnostic) ==
        gneiss::engine::mesh_binary_result::success);
  {
    std::ofstream output(files.root / "g.mesh.json", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::string message;
  check(
      gneiss::engine::encode_texture_ktx2(
          {.transfer = gneiss::engine::texture_transfer::srgb,
           .levels = {{.width = 2,
                       .height = 1,
                       .pixels = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                                  std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}}},
                      {.width = 1,
                       .height = 1,
                       .pixels = {std::byte{188}, std::byte{0}, std::byte{188}, std::byte{255}}}}},
          bytes, message) == gneiss::engine::texture_ktx2_result::success);
  {
    std::ofstream output(files.root / "image.ktx2", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  if (test.layers || test.receiver) {
    std::ofstream(files.root / "blue.material.json")
        << R"({"format":"gneiss.material","version":5,"color":[0,0,1,)"
        << (test.foreground || test.receiver ? "1" : "0.5")
        << R"(],"base_color_texture":null,"metallic":0,"roughness":1,"alpha_mode":")"
        << (test.foreground || test.receiver ? "OPAQUE" : "BLEND") << "\"}";
    std::ifstream input(files.root / "s.scene.json");
    std::string scene_text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    input.close();
    std::string object =
        R"({"uuid":"00000000-0000-4000-8000-000000000004","parent":null,"transform":{"translation":[0,0,)";
    object += test.foreground ? "0.2" : "-0.2";
    object +=
        R"(],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://g.mesh.json","material":"asset://blue.material.json"}}})";
    if (test.receiver) {
      constexpr std::string_view scale = "\"scale\":[1,1,1]";
      object.replace(object.find(scale), scale.size(), "\"scale\":[4,4,1]");
    }
    if (test.reverse_submission)
      scene_text.insert(scene_text.find("\"objects\":[") + 11U, object + ',');
    else
      scene_text.insert(scene_text.rfind("],\"prefab_instances\""), ',' + object);
    std::ofstream(files.root / "s.scene.json") << scene_text;
  }
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance_id scene{};
  check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene)} ==
        gneiss::engine::result::success);
  check(gneiss::engine::result{app->run(3U)} == gneiss::engine::result::success);
  gneiss::engine::frame_image image;
  check(app->capture_frame(128U, 128U, image) == gneiss::engine::result::success);
  if (frame != nullptr)
    *frame = image.pixels;
  constexpr auto offset = (64U * 128U + 64U) * 4U;
  return {std::to_integer<unsigned>(image.pixels[offset]),
          std::to_integer<unsigned>(image.pixels[offset + 1]),
          std::to_integer<unsigned>(image.pixels[offset + 2])};
}
void run_material_states() {
  const auto opaque = state_pixel({});
  const auto background = state_pixel({.mode = "MASK", .alpha = 0.25F});
  check(opaque != background);
  check(state_pixel({.mode = "MASK", .alpha = 0.75F}) == opaque);
  check(state_pixel({.mode = "MASK", .alpha = 0.75F, .cutoff = 0.9F}) == background);
  check(state_pixel({.back = true}) == background);
  check(state_pixel({.back = true, .double_sided = true}) != background);
  check(state_pixel({.color = {0, 0, 0, 1}})[0] < opaque[0]);
  check(state_pixel({.mode = "MASK", .color = {1, 1, 1, 0.25F}}) == background);
  const auto uv1 = state_pixel({.uv_set = 1});
  check(opaque[0] > opaque[2] + 30U && uv1[2] > uv1[0] + 30U);
  check(state_pixel({.wrap = 1}) == uv1);
  check(state_pixel({.wrap = 2}) == uv1);
  check(state_pixel({.mode = "BLEND", .alpha = 0}) == background);
  const auto blend = state_pixel({.mode = "BLEND", .alpha = 0.5F});
  check(blend != opaque && blend != background);
  const auto layers = state_pixel({.mode = "BLEND", .alpha = 0.5F, .layers = true});
  check(layers ==
        state_pixel({.mode = "BLEND", .alpha = 0.5F, .layers = true, .reverse_submission = true}));
  check(layers[0] > layers[2]);
  const auto foreground =
      state_pixel({.mode = "BLEND", .alpha = 0.5F, .layers = true, .foreground = true});
  check(foreground[2] > foreground[0] + 30U);
  // 透明 Alpha 0 不投影；MASK 裁掉的整个物体也不能留下实心阴影。
  std::vector<std::byte> invisible_blend, invisible_mask, opaque_shadow, mask_shadow;
  (void)state_pixel({.mode = "BLEND", .alpha = 0, .receiver = true}, &invisible_blend);
  (void)state_pixel({.mode = "MASK", .alpha = 0, .receiver = true}, &invisible_mask);
  (void)state_pixel({.receiver = true}, &opaque_shadow);
  (void)state_pixel({.mode = "MASK", .receiver = true}, &mask_shadow);
  check(invisible_blend == invisible_mask);
  check(opaque_shadow == mask_shadow && opaque_shadow != invisible_mask);
}

void run_material_channels() {
  constexpr auto zero = std::byte{0};
  constexpr auto half = std::byte{128};
  constexpr auto full = std::byte{255};
  // 通道契约：MR 的 R 无效、G/B 有效；AO 只读取 R。
  const auto dielectric = material_pixel("metallic_roughness_texture", {zero, full, zero, full});
  check(dielectric == material_pixel("metallic_roughness_texture", {full, full, zero, full}));
  check(dielectric != material_pixel("metallic_roughness_texture", {zero, half, zero, full}));
  check(dielectric != material_pixel("metallic_roughness_texture", {zero, full, full, full}));
  const auto occluded = material_pixel("occlusion_texture", {zero, full, full, full});
  check(occluded == material_pixel("occlusion_texture", {zero, zero, zero, full}));
  check(occluded[0] < material_pixel("occlusion_texture", {full, full, full, full})[0]);
  // 明确的 +Y/-Y 法线在固定非对称灯光下不能产生同一结果。
  check(material_pixel("normal_texture", {half, full, half, full}) !=
        material_pixel("normal_texture", {half, zero, half, full}));
}

void run(gneiss::engine::execution_mode mode, bool pbr = false) {
  fixture files(pbr);
  files.color(std::byte{20}, std::byte{240});
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  gneiss::scene_instance_id scene{};
  constexpr std::string_view uri = "asset://s.scene.json";
  check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
        gneiss::engine::result::success);
  check(gneiss::engine::result{app->run(3U)} == gneiss::engine::result::success);
  const auto capture = [&](unsigned width = 128U) {
    gneiss::engine::frame_image image;
    const auto result = app->capture_frame(width, 128U, image);
    if (result != gneiss::engine::result::success) {
      std::fprintf(stderr, "capture=%d\n", result.code());
    }
    check(result == gneiss::engine::result::success && image.pixels.size() == width * 128U * 4U);
    return image;
  };
  const auto center = [](const auto& image) {
    const auto offset = (image.height / 2U * image.width + image.width / 2U) * 4U;
    return std::array{std::to_integer<unsigned>(image.pixels[offset]),
                      std::to_integer<unsigned>(image.pixels[offset + 1U]),
                      std::to_integer<unsigned>(image.pixels[offset + 2U])};
  };
  const auto await_capture = [&](bool is_blue) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto image = capture();
    auto color = center(image);
    while (!(is_blue ? color[2] > color[0] + 30U : color[0] > color[2] + 30U) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      image = capture();
      color = center(image);
    }
    return image;
  };
  const auto before = await_capture(true);
  const auto blue = center(before);
  std::printf("before=%u,%u,%u\n", blue[0], blue[1], blue[2]);
  check(blue[2] > blue[0] + 30U);
  const auto reload = [&](bool mixed = false) {
    using type = gneiss::engine::asset_type;
    const std::vector<gneiss::engine::asset_source> assets =
        mixed
            ? std::vector<gneiss::engine::asset_source>{{"asset://g.mesh.json", type::mesh},
                                                        {"asset://m.material.json", type::material}}
            : std::vector<gneiss::engine::asset_source>{{"asset://t.texture.json", type::texture}};
    std::uint64_t request{};
    check(app->request_assets(assets, 1U, 1U, request, true) == gneiss::engine::result::success);
    gneiss::engine::asset_load_completion completed;
    bool ready{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(app->poll_assets(completed, ready) == gneiss::engine::result::success);
      std::this_thread::yield();
    }
    return completed.result;
  };
  files.color(std::byte{240}, std::byte{20});
  check(reload() == gneiss::engine::result::success);
  const auto after = await_capture(false);
  const auto red = center(after);
  std::printf("after=%u,%u,%u\n", red[0], red[1], red[2]);
  check(red[0] > red[2] + 30U && before.pixels != after.pixels);
  std::ofstream(files.root / "image.ktx2") << "broken";
  check(reload() != gneiss::engine::result::success);
  check(capture().pixels == after.pixels);
  const auto resized = capture(192U);
  const auto resized_red = center(resized);
  check(resized_red[0] > resized_red[2] + 30U);
  // 材质引入未显式请求的新依赖，网格同时缩小；一次发布后像素颜色和覆盖范围都必须改变。
  files.color(std::byte{20}, std::byte{240});
  std::ofstream(files.root / "new.texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":5,"color":[1,1,1,1],"base_color_texture":"asset://new.texture.json","metallic":0,"roughness":1})";
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.3,-0.3,0],[0.3,-0.3,0],[0,0.3,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  check(reload(true) == gneiss::engine::result::success);
  const auto mixed = await_capture(true);
  const auto mixed_color = center(mixed);
  check(mixed_color[2] > mixed_color[0] + 30U);
  const auto coverage = [](const auto& image) {
    std::size_t count{};
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U) {
      if (image.pixels[offset] != image.pixels[0] || image.pixels[offset + 1U] != image.pixels[1] ||
          image.pixels[offset + 2U] != image.pixels[2])
        ++count;
    }
    return count;
  };
  check(coverage(mixed) > 0U && coverage(mixed) < coverage(after) / 2U);
  // 准备阶段遇到损坏网格时，本批尚未进入采用，已有画面保持不变。
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":5,"color":[1,0,0,1],"metallic":0,"roughness":1})";
  std::ofstream(files.root / "g.mesh.json") << "broken";
  check(reload(true) != gneiss::engine::result::success);
  check(capture().pixels == mixed.pixels);
  check(center(capture(192U))[2] > center(capture(192U))[0] + 30U);
}
void run_scene(gneiss::engine::execution_mode mode, bool pbr = false) {
  using namespace runtime;
  fixture files(pbr);
  files.color(std::byte{20}, std::byte{240});
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::engine::result{runtime::application::create(desc, app)} ==
        gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance_id scene{};
  check(gneiss::engine::result{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
        gneiss::engine::result::success);
  check(gneiss::engine::result{app->run(3U)} == gneiss::engine::result::success);
  const auto capture = [&] {
    gneiss::engine::frame_image image;
    check(app->capture_frame(128U, 128U, image) == gneiss::engine::result::success);
    return image.pixels;
  };
  auto active_pixels = capture();
  // 打开场景只复用已有成功内容；外部文件更新由显式重载处理。
  const auto load = [&](std::uint64_t revision, bool cancel) {
    const auto old_scene = scene;
    std::uint64_t request{};
    check(app->request_scene(uri, 1U, revision, request) == gneiss::engine::result::success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool requested_cancel{};
    scene_load_completion completion;
    while (true) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::engine::execution_mode::cooperative)
        (void)scheduler.run_ready();
      bool terminal{};
      check(app->poll_scene(completion, terminal) == gneiss::engine::result::success);
      if (terminal) {
        check(requested_cancel && completion.progress.phase == scene_load_phase::cancelled);
        check(capture() == active_pixels);
        break;
      }
      scene_load_progress progress;
      check(app->scene_progress(progress));
      if (progress.phase == scene_load_phase::ready) {
        const auto ready_pixels = capture();
        check(ready_pixels == active_pixels);
        active_pixels = ready_pixels;
        if (cancel) {
          check(app->cancel_scene(request) == gneiss::engine::result::success);
          requested_cancel = true;
        } else {
          check(app->activate_scene(request, completion) == gneiss::engine::result::success);
          scene = completion.scene;
          auto native = app;
          while (native->visual_assets_loading()) {
            check(std::chrono::steady_clock::now() < deadline);
            if (mode == gneiss::engine::execution_mode::cooperative) {
              (void)scheduler.run_ready();
            }
            native->advance_scene_load();
          }
          check(app->poll_scene(completion, terminal) == gneiss::engine::result::success);
          check(gneiss::engine::result{app->run(3U)} == gneiss::engine::result::success);
          const auto changed = capture();
          check(changed == active_pixels);
          active_pixels = changed;
          std::uint64_t count{};
          check(gneiss::engine::result{app->scenes()->get_node_count(old_scene, &count)} ==
                gneiss::engine::result::invalid_handle);
          break;
        }
      }
      std::this_thread::yield();
    }
    const auto statistics = app->scene_retirement();
    check(statistics.live_resources == 3U && !statistics.pending);
  };
  files.color(std::byte{240}, std::byte{20});
  load(1U, false);
  files.color(std::byte{20}, std::byte{240});
  load(2U, true);
  std::ofstream(files.root / "image.ktx2") << "broken";
  load(3U, false);
  files.color(std::byte{20}, std::byte{240});
  load(4U, false);
}
}
int main(int argc, char* argv[]) try {
  if (argc == 2 && std::string_view{argv[1]} == "--probe-mask") {
    const auto pixel = state_pixel({.mode = "MASK", .alpha = 0.25F});
    std::printf("mask probe: %u,%u,%u\n", pixel[0], pixel[1], pixel[2]);
    return 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--probe-negative-scale") {
    const auto white = std::array{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    const auto positive_visible = material_pixel("emissive_texture", white);
    const auto negative_visible = material_pixel("emissive_texture", white, true);
    std::printf("visibility control: %u,%u,%u / %u,%u,%u\n", positive_visible[0],
                positive_visible[1], positive_visible[2], negative_visible[0], negative_visible[1],
                negative_visible[2]);
    check(positive_visible[0] > 100U && negative_visible[0] > 100U);
    const auto normal = std::array{std::byte{128}, std::byte{255}, std::byte{128}, std::byte{255}};
    const auto positive = material_pixel("normal_texture", normal);
    const auto negative = material_pixel("normal_texture", normal, true);
    std::printf(
        "negative-scale probe: visible=%u/%u normal-positive=%u,%u,%u normal-negative=%u,%u,%u\n",
        positive_visible[0], negative_visible[0], positive[0], positive[1], positive[2],
        negative[0], negative[1], negative[2]);
    // 保留相同网格绕序；固定纯色法线沿 +Y，仅反射 X 不应翻转 B 或导致对象被剔除。
    // RGBA8 的 128 解码为 +1/255 而非零；反射 X 会改变这点残余 X 分量。
    // 允许两个量化级，仍能区分旧实现约 80 级的 Y 手性错误。
    for (std::size_t channel = 0; channel < positive.size(); ++channel) {
      const auto delta = static_cast<int>(positive[channel]) - static_cast<int>(negative[channel]);
      if (delta < -2 || delta > 2) {
        return 2;
      }
    }
    return 0;
  }
  std::fprintf(stderr, "asset-pixel: pending resources\n");
  run_asset_reload();
  run_pending_resources();
  std::fprintf(stderr, "asset-pixel: mip\n");
  run_mip_sampling();
  std::fprintf(stderr, "asset-pixel: nonzero variant payload\n");
  run_mip_sampling(true);
  std::fprintf(stderr, "asset-pixel: selected variant async\n");
  run_mip_sampling(true, true);
  std::fprintf(stderr, "asset-pixel: channels\n");
  run_material_channels();
  std::fprintf(stderr, "asset-pixel: states\n");
  run_material_states();
  run(gneiss::engine::execution_mode::thread_pool, true);
  run(gneiss::engine::execution_mode::cooperative, true);
  run_scene(gneiss::engine::execution_mode::thread_pool, true);
  run_scene(gneiss::engine::execution_mode::cooperative, true);
  run(gneiss::engine::execution_mode::thread_pool);
  run(gneiss::engine::execution_mode::cooperative);
  run_scene(gneiss::engine::execution_mode::thread_pool);
  run_scene(gneiss::engine::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
