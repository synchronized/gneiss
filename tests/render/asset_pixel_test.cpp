// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_asset_reload_internal.hpp"
#include "engine/function/application/application_scene_load_internal.hpp"
#include "engine/asset/mesh_binary.hpp"
#include "engine/asset/texture_binary.hpp"
#include "engine/asset/texture_ktx2.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/application.hpp>
#include <gneiss/scene.h>
#include <granit/asset_tools/texture_builder.hpp>
#include <granit/renderer/texture_asset.hpp>
#include <source_location>
#include <stdexcept>

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
        << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
    std::ofstream(root / "m.material.json")
        << R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://t.texture.json","metallic":0,"roughness":1})";
    if (pbr) {
      std::ofstream(root / "m.material.json")
          << R"({"format":"gneiss.material","version":4,"color":[0,0,0,1],"base_color_texture":null,"metallic":0,"roughness":1,"emissive":[1,1,1],"emissive_texture":"asset://t.texture.json"})";
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
    check(asset_internal::encode_texture_ktx2(
              {.transfer = asset_internal::texture_transfer::srgb,
               .levels = {{.width = 1U,
                           .height = 1U,
                           .pixels = {red, std::byte{20}, blue, std::byte{255}}}}},
              bytes, message) == asset_internal::texture_ktx2_result::success);
    std::ofstream stream(root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
};
void run_mip_sampling(bool packaged = false, bool async = false) {
  fixture files(true);
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.8,-0.7,0],[0.8,-0.7,0],[0,0.8,0]],"uvs":[[0,0],[1024,0],[512,1024]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  asset_internal::texture_ktx2 texture;
  texture.transfer = asset_internal::texture_transfer::srgb;
  for (std::uint32_t width = 8U; width > 0U; width /= 2U) {
    asset_internal::texture_mip mip{.width = width, .height = width, .pixels = {}};
    for (std::uint32_t pixel = 0; pixel < width * width; ++pixel) {
      mip.pixels.insert(mip.pixels.end(),
                        {width == 8U ? std::byte{0} : std::byte{255}, std::byte{0},
                         width == 8U ? std::byte{255} : std::byte{0}, std::byte{255}});
    }
    texture.levels.push_back(std::move(mip));
  }
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(asset_internal::encode_texture_ktx2(texture, bytes, diagnostic) ==
        asset_internal::texture_ktx2_result::success);
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
    check(asset_internal::encode_texture_binary(built.manifest(), built.payload(), bytes,
                                                diagnostic) ==
          asset_internal::texture_binary_result::success);
    std::ofstream(files.root / "t.texture.json")
        << R"({"format":"gneiss.texture","version":1,"source":"asset://image.gneiss-texture","color_space":"srgb"})";
  }
  {
    std::ofstream stream(files.root / (packaged ? "image.gneiss-texture" : "image.ktx2"),
                         std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  tasks::task_scheduler scheduler({.workers = 1U});
  application app;
  const auto root = files.root.string();
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_flags &= ~GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create_native(desc, app) == result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss_scene_instance scene{};
  if (async) {
    using namespace application_internal;
    check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
    std::uint64_t request{};
    check(request_scene_load(app.get(), uri, 1U, 1U, request) == GNEISS_SUCCESS);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (true) {
      check(std::chrono::steady_clock::now() < deadline);
      scene_load_completion completion;
      bool terminal{};
      check(poll_scene_load(app.get(), completion, terminal) == GNEISS_SUCCESS);
      if (terminal)
        std::fprintf(stderr, "scene result %d: %s\n", completion.result,
                     completion.message.c_str());
      check(!terminal);
      scene_load_progress progress;
      bool available{};
      check(query_scene_load_progress(app.get(), progress, available) == GNEISS_SUCCESS &&
            available);
      if (progress.phase == scene_load_phase::ready) {
        check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
        check(completion.progress.texture_payload_bytes > 0U);
        check(completion.progress.peak_upload_bytes > 0U);
        // 小纹理的 Manifest 可大于负载；分别核对逻辑负载与两个元数据副本。
        asset_internal::texture_binary_view view;
        std::string message;
        check(asset_internal::decode_texture_binary(bytes, view, message) ==
              asset_internal::texture_binary_result::success);
        check(completion.progress.texture_payload_bytes == 340U);
        check(completion.progress.resident_bytes + view.manifest.size() ==
              completion.progress.cpu_data_bytes + completion.progress.texture_payload_bytes);
        break;
      }
      std::this_thread::yield();
    }
  } else {
    check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  }
  check(app.run(3U) == result::success);
  render_internal::frame_image image;
  check(application_internal::capture_frame(app.get(), 128U, 128U, image) == GNEISS_SUCCESS);
  const auto offset = (64U * 128U + 64U) * 4U;
  // LOD0 全蓝，缩小时应采样全红的较低分辨率层；阈值不依赖具体色调映射曲线。
  check(std::to_integer<unsigned>(image.pixels[offset]) >
        std::to_integer<unsigned>(image.pixels[offset + 2U]) + 30U);
}

std::array<unsigned, 3> material_pixel(std::string_view slot, std::array<std::byte, 4> pixel,
                                       bool reflected = false) {
  fixture files;
  std::ofstream(files.root / "t.texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"linear"})";
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":4,"color":[0.5,0.5,0.5,1],"base_color_texture":null,"metallic":1,"roughness":1,")"
      << slot << R"(":"asset://t.texture.json","emissive":[)"
      << (slot == "emissive_texture" ? "1,1,1" : "0,0,0") << "]}";
  asset_internal::mesh_binary_data mesh{.vertices = {{{-0.8F, -0.7F, 0.0F}, {0, 0}, {0, 0, 1}},
                                                     {{0.8F, -0.7F, 0.0F}, {1, 0}, {0, 0, 1}},
                                                     {{0.0F, 0.8F, 0.0F}, {0.5F, 1}, {0, 0, 1}}},
                                        .indices = {0U, 1U, 2U},
                                        .tangents = {{1, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}}};
  std::vector<std::byte> bytes;
  asset_internal::mesh_binary_diagnostic mesh_diagnostic;
  check(asset_internal::encode_mesh_binary(mesh, bytes, mesh_diagnostic) ==
        asset_internal::mesh_binary_result::success);
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
  check(asset_internal::encode_texture_ktx2(
            {.transfer = asset_internal::texture_transfer::linear,
             .levels = {{.width = 1U, .height = 1U, .pixels = {pixel.begin(), pixel.end()}}}},
            bytes, diagnostic) == asset_internal::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  application app;
  const auto root = files.root.string();
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_flags &= ~GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create_native(desc, app) == result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss_scene_instance scene{};
  check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  check(app.run(3U) == result::success);
  render_internal::frame_image image;
  check(application_internal::capture_frame(app.get(), 128U, 128U, image) == GNEISS_SUCCESS);
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
  asset_internal::mesh_binary_data mesh;
  mesh.vertices = {{{-0.8F, -0.7F, 0}, {1.25F, 0.5F}, {0, 0, 1}},
                   {{0.8F, -0.7F, 0}, {1.25F, 0.5F}, {0, 0, 1}},
                   {{0, 0.8F, 0}, {1.25F, 0.5F}, {0, 0, 1}}};
  mesh.indices =
      test.back ? std::vector<std::uint32_t>{2, 1, 0} : std::vector<std::uint32_t>{0, 1, 2};
  mesh.tangents.assign(3, {1, 0, 0, 1});
  mesh.uv1.assign(3, {0.75F, 0.5F});
  mesh.colors.assign(3, test.color);
  std::vector<std::byte> bytes;
  asset_internal::mesh_binary_diagnostic diagnostic;
  check(asset_internal::encode_mesh_binary(mesh, bytes, diagnostic) ==
        asset_internal::mesh_binary_result::success);
  {
    std::ofstream output(files.root / "g.mesh.json", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::string message;
  check(
      asset_internal::encode_texture_ktx2(
          {.transfer = asset_internal::texture_transfer::srgb,
           .levels = {{.width = 2,
                       .height = 1,
                       .pixels = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                                  std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}}},
                      {.width = 1,
                       .height = 1,
                       .pixels = {std::byte{188}, std::byte{0}, std::byte{188}, std::byte{255}}}}},
          bytes, message) == asset_internal::texture_ktx2_result::success);
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
  application app;
  const auto root = files.root.string();
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_flags &= ~GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create_native(desc, app) == result::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss_scene_instance scene{};
  check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  check(app.run(3U) == result::success);
  render_internal::frame_image image;
  check(application_internal::capture_frame(app.get(), 128U, 128U, image) == GNEISS_SUCCESS);
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

void run(tasks::execution_mode mode, bool pbr = false) {
  fixture files(pbr);
  files.color(std::byte{20}, std::byte{240});
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  const auto root = files.root.string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_flags &= ~GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create_native(desc, app) == result::success);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  gneiss_scene_instance scene{};
  constexpr std::string_view uri = "asset://s.scene.json";
  check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  check(app.run(3U) == result::success);
  const auto capture = [&](unsigned width = 128U) {
    render_internal::frame_image image;
    const auto result = application_internal::capture_frame(app.get(), width, 128U, image);
    if (result != GNEISS_SUCCESS) {
      std::fprintf(stderr, "capture=%d\n", result);
    }
    check(result == GNEISS_SUCCESS && image.pixels.size() == width * 128U * 4U);
    return image;
  };
  const auto center = [](const auto& image) {
    const auto offset = (image.height / 2U * image.width + image.width / 2U) * 4U;
    return std::array{std::to_integer<unsigned>(image.pixels[offset]),
                      std::to_integer<unsigned>(image.pixels[offset + 1U]),
                      std::to_integer<unsigned>(image.pixels[offset + 2U])};
  };
  const auto before = capture();
  const auto blue = center(before);
  std::printf("before=%u,%u,%u\n", blue[0], blue[1], blue[2]);
  check(blue[2] > blue[0] + 30U);
  const auto reload = [&](bool mixed = false) {
    using type = render_internal::render_asset_type;
    const std::vector<render_internal::render_asset_reload> assets =
        mixed
            ? std::vector<render_internal::render_asset_reload>{{"asset://g.mesh.json", type::mesh},
                                                                {"asset://m.material.json",
                                                                 type::material}}
            : std::vector<render_internal::render_asset_reload>{
                  {"asset://t.texture.json", type::texture}};
    std::uint64_t request{};
    check(application_internal::request_render_assets(app.get(), assets, 1U, 1U, request) ==
          GNEISS_SUCCESS);
    render_internal::texture_load_completion completed;
    bool ready{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(application_internal::poll_textures(app.get(), completed, ready) == GNEISS_SUCCESS);
      std::this_thread::yield();
    }
    return completed.result;
  };
  files.color(std::byte{240}, std::byte{20});
  check(reload() == GNEISS_SUCCESS);
  const auto after = capture();
  const auto red = center(after);
  std::printf("after=%u,%u,%u\n", red[0], red[1], red[2]);
  check(red[0] > red[2] + 30U && before.pixels != after.pixels);
  std::ofstream(files.root / "image.ktx2") << "broken";
  check(reload() != GNEISS_SUCCESS);
  check(capture().pixels == after.pixels);
  const auto resized = capture(192U);
  const auto resized_red = center(resized);
  check(resized_red[0] > resized_red[2] + 30U);
  // 材质引入未显式请求的新依赖，网格同时缩小；一次发布后像素颜色和覆盖范围都必须改变。
  files.color(std::byte{20}, std::byte{240});
  std::ofstream(files.root / "new.texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://new.texture.json","metallic":0,"roughness":1})";
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.3,-0.3,0],[0.3,-0.3,0],[0,0.3,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  check(reload(true) == GNEISS_SUCCESS);
  const auto mixed = capture();
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
  // 同批有效材质与损坏网格不可部分发布。
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":3,"color":[1,0,0,1],"metallic":0,"roughness":1})";
  std::ofstream(files.root / "g.mesh.json") << "broken";
  check(reload(true) != GNEISS_SUCCESS);
  check(capture().pixels == mixed.pixels);
  check(center(capture(192U))[2] > center(capture(192U))[0] + 30U);
}
void run_scene(tasks::execution_mode mode, bool pbr = false) {
  using namespace application_internal;
  fixture files(pbr);
  files.color(std::byte{20}, std::byte{240});
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  const auto root = files.root.string();
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_flags &= ~GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create_native(desc, app) == result::success);
  check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss_scene_instance scene{};
  check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  check(app.run(3U) == result::success);
  const auto capture = [&] {
    render_internal::frame_image image;
    check(capture_frame(app.get(), 128U, 128U, image) == GNEISS_SUCCESS);
    return image.pixels;
  };
  auto active_pixels = capture();
  // 私有 GPU 候选 ready 后旧图像仍完全不变；只允许激活后发布新颜色。
  const auto load = [&](std::uint64_t revision, bool cancel, bool fail) {
    const auto old_scene = scene;
    std::uint64_t request{};
    check(request_scene_load(app.get(), uri, 1U, revision, request) == GNEISS_SUCCESS);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool requested_cancel{};
    scene_load_completion completion;
    while (true) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == tasks::execution_mode::cooperative)
        (void)scheduler.run_ready();
      bool terminal{};
      check(poll_scene_load(app.get(), completion, terminal) == GNEISS_SUCCESS);
      if (terminal) {
        check((fail && completion.progress.phase == scene_load_phase::failed) ||
              (requested_cancel && completion.progress.phase == scene_load_phase::cancelled));
        check(capture() == active_pixels);
        break;
      }
      scene_load_progress progress;
      bool available{};
      check(query_scene_load_progress(app.get(), progress, available) == GNEISS_SUCCESS &&
            available);
      if (progress.phase == scene_load_phase::ready) {
        check(!fail && capture() == active_pixels);
        if (cancel) {
          check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
          requested_cancel = true;
        } else {
          check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
          scene = completion.scene;
          check(poll_scene_load(app.get(), completion, terminal) == GNEISS_SUCCESS);
          check(app.run(3U) == result::success);
          const auto changed = capture();
          check(changed != active_pixels);
          active_pixels = changed;
          std::uint64_t count{};
          check(gneiss_scene_instance_get_node_count(app.get(), old_scene, &count) ==
                GNEISS_ERROR_INVALID_HANDLE);
          break;
        }
      }
      std::this_thread::yield();
    }
    scene_retirement_statistics statistics;
    check(query_scene_retirement(app.get(), statistics) == GNEISS_SUCCESS &&
          statistics.live_resources == 3U && !statistics.pending);
  };
  files.color(std::byte{240}, std::byte{20});
  load(1U, false, false);
  files.color(std::byte{20}, std::byte{240});
  load(2U, true, false);
  std::ofstream(files.root / "image.ktx2") << "broken";
  load(3U, false, true);
  files.color(std::byte{20}, std::byte{240});
  load(4U, false, false);
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
  run(gneiss::tasks::execution_mode::thread_pool, true);
  run(gneiss::tasks::execution_mode::cooperative, true);
  run_scene(gneiss::tasks::execution_mode::thread_pool, true);
  run_scene(gneiss::tasks::execution_mode::cooperative, true);
  run(gneiss::tasks::execution_mode::thread_pool);
  run(gneiss::tasks::execution_mode::cooperative);
  run_scene(gneiss::tasks::execution_mode::thread_pool);
  run_scene(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
