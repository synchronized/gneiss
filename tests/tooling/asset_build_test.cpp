// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/asset_build.hpp"

#include "engine/asset/texture_binary.hpp"

#include <granit/renderer/texture_asset.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace gneiss::tooling::asset_build;

[[nodiscard]] bool write(const std::filesystem::path& path, const std::string& content) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << content;
  return !error && stream.good();
}

[[nodiscard]] std::string png_fixture(char payload) {
  constexpr unsigned char png[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00,
      0x00, 0xB5, 0x1C, 0x0C, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00, 0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xE3, 0x66,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
  std::string output{reinterpret_cast<const char*>(png), sizeof(png)};
  output.push_back(payload);
  return output;
}

[[nodiscard]] const build_output* find_output(const build_report& report, const std::string& path) {
  for (const auto& output : report.outputs) {
    if (output.relative_path == path) {
      return &output;
    }
  }
  return nullptr;
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  const auto size = stream.tellg();
  if (size <= 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()), size);
  return stream.good() ? bytes : std::vector<std::byte>{};
}

} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-asset-build-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto source = root / "source";
  const auto cache = root / "build" / ".gneiss-cache";
  if (!write(source / "scenes/main.scene.json",
             R"({"material":"asset://materials/a.material.json"})") ||
      !write(source / "materials/a.material.json", R"({"texture":"asset://textures/a.png"})") ||
      !write(source / "textures/a.png", png_fixture('a')) ||
      !write(source / "textures/unused.png", "被 Shipping 裁剪的无效 PNG") ||
      !write(source / "source/original.txt", "不进入运行时资产")) {
    return 1;
  }

  auto registry = make_default_registry();
  if (registry.find("a.material.json") == nullptr ||
      registry.find("a.material.json")->id != "gneiss.material" ||
      registry.register_processor({.id = "gneiss.texture", .version = 2U, .suffixes = {".x"}})) {
    return 2;
  }

  const build_request development{.source_root = source,
                                  .output_root = root / "development-a",
                                  .cache_root = cache,
                                  .root_uris = {},
                                  .target_platform = "test",
                                  .target_architecture = "test",
                                  .profile = build_profile::development,
                                  .progress = {}};
  const auto first = build_assets(development, registry);
  if (first.result != build_result::processor_failed) {
    return 3;
  }
  if (!write(source / "textures/unused.png", png_fixture('u'))) {
    return 3;
  }
  const auto first_valid = build_assets(development, registry);
  if (first_valid.result != build_result::success || first_valid.source_count != 4U ||
      first_valid.built_count != 4U || first_valid.cache_hit_count != 0U ||
      first_valid.pruned_count != 0U ||
      std::filesystem::exists(development.output_root / "source/original.txt") ||
      !std::filesystem::exists(development.output_root / "textures/a.gneiss-texture") ||
      std::filesystem::exists(development.output_root / "textures/a.png")) {
    return 3;
  }
  const auto texture_bytes = read_bytes(development.output_root / "textures/a.gneiss-texture");
  gneiss::asset_internal::texture_binary_view texture_binary;
  std::string texture_diagnostic;
  granit::texture_asset_info texture_info;
  if (gneiss::asset_internal::decode_texture_binary(texture_bytes, texture_binary,
                                                    texture_diagnostic) !=
          gneiss::asset_internal::texture_binary_result::success ||
      granit::inspect_texture_asset(texture_binary.manifest, texture_info) !=
          granit::result::success ||
      texture_info.variants.size() != 2U ||
      texture_info.variants[0].format != granit::texture_format::bc7_rgba_srgb ||
      texture_info.variants[1].format != granit::texture_format::rgba8_srgb) {
    return 3;
  }
  auto second_request = development;
  second_request.output_root = root / "development-b";
  const auto second = build_assets(second_request, registry);
  if (second.result != build_result::success || second.cache_hit_count != 4U ||
      second.built_count != 0U) {
    return 4;
  }

  auto shipping_request = development;
  shipping_request.output_root = root / "shipping";
  shipping_request.profile = build_profile::shipping;
  shipping_request.root_uris = {"asset://scenes/main.scene.json"};
  if (!write(source / "textures/unused.png", "被 Shipping 裁剪的无效 PNG")) {
    return 5;
  }
  const auto shipping = build_assets(shipping_request, registry);
  if (shipping.result != build_result::success || shipping.outputs.size() != 3U ||
      shipping.pruned_count != 1U ||
      std::filesystem::exists(shipping_request.output_root / "textures/unused.gneiss-texture")) {
    return 5;
  }

  const auto* old_scene = find_output(first_valid, "scenes/main.scene.json");
  if (old_scene == nullptr || !write(source / "textures/unused.png", png_fixture('u')) ||
      !write(source / "textures/a.png", png_fixture('b'))) {
    return 6;
  }
  auto changed_request = development;
  changed_request.output_root = root / "development-c";
  const auto changed = build_assets(changed_request, registry);
  const auto* new_scene = find_output(changed, "scenes/main.scene.json");
  if (changed.result != build_result::success || new_scene == nullptr ||
      old_scene->cache_key == new_scene->cache_key || changed.built_count != 3U ||
      changed.cache_hit_count != 1U) {
    return 7;
  }

  auto missing_request = shipping_request;
  missing_request.output_root = root / "missing";
  missing_request.root_uris = {"asset://missing.json"};
  if (build_assets(missing_request, registry).result != build_result::dependency_missing) {
    return 8;
  }

  const auto pbr_source = root / "pbr-source";
  const std::string color_desc =
      R"({"format":"gneiss.texture","version":1,"source":"asset://color.png","color_space":"srgb"})";
  const std::string data_desc =
      R"({"format":"gneiss.texture","version":1,"source":"asset://data.png","color_space":"linear"})";
  const std::string normal_desc =
      R"({"format":"gneiss.texture","version":2,"source":"asset://normal.png","color_space":"linear","usage":"normal"})";
  if (!write(pbr_source / "color.png", png_fixture('p')) ||
      !write(pbr_source / "data.png", png_fixture('p')) ||
      !write(pbr_source / "normal.png", png_fixture('p')) ||
      !write(pbr_source / "color.texture.json", color_desc) ||
      !write(pbr_source / "data.texture.json", data_desc) ||
      !write(pbr_source / "normal.texture.json", normal_desc) ||
      !write(
          pbr_source / "material.material.json",
          R"({"format":"gneiss.material","version":4,"color":[1,1,1,1],"metallic":1,"roughness":1,"base_color_texture":"asset://color.texture.json","metallic_roughness_texture":"asset://data.texture.json","normal_texture":"asset://normal.texture.json","occlusion_texture":"asset://data.texture.json","emissive_texture":"asset://color.texture.json"})")) {
    return 9;
  }
  auto pbr_request = shipping_request;
  pbr_request.source_root = pbr_source;
  pbr_request.output_root = root / "pbr-output";
  pbr_request.root_uris = {"asset://material.material.json"};
  const auto pbr_first = build_assets(pbr_request, registry);
  pbr_request.output_root = root / "pbr-cached";
  const auto pbr_cached = build_assets(pbr_request, registry);
  if (pbr_first.result != build_result::success || pbr_first.outputs.size() != 7U ||
      pbr_cached.result != build_result::success || pbr_cached.cache_hit_count != 7U ||
      pbr_cached.built_count != 0U) {
    std::fprintf(stderr, "PBR Cook: first=%d outputs=%zu cached=%d hits=%zu built=%zu\n",
                 static_cast<int>(pbr_first.result), pbr_first.outputs.size(),
                 static_cast<int>(pbr_cached.result),
                 static_cast<std::size_t>(pbr_cached.cache_hit_count),
                 static_cast<std::size_t>(pbr_cached.built_count));
    return 10;
  }
  const auto* old_normal = find_output(pbr_first, "normal.gneiss-texture");
  if (!write(
          pbr_source / "normal.texture.json",
          R"({"format":"gneiss.texture","version":1,"source":"asset://normal.png","color_space":"linear"})")) {
    return 11;
  }
  pbr_request.output_root = root / "pbr-changed";
  const auto pbr_changed = build_assets(pbr_request, registry);
  const auto* new_normal = find_output(pbr_changed, "normal.gneiss-texture");
  if (pbr_changed.result != build_result::success || old_normal == nullptr ||
      new_normal == nullptr || old_normal->cache_key == new_normal->cache_key ||
      pbr_changed.built_count != 2U || pbr_changed.cache_hit_count != 5U) {
    std::fprintf(stderr, "PBR changed: result=%d old=%d new=%d built=%zu\n",
                 static_cast<int>(pbr_changed.result), old_normal != nullptr, new_normal != nullptr,
                 static_cast<std::size_t>(pbr_changed.built_count));
    return 12;
  }
  pbr_request.output_root = root / "pbr-invalid";
  if (!write(
          pbr_source / "normal.texture.json",
          R"({"format":"gneiss.texture","version":2,"source":"asset://normal.png","color_space":"srgb","usage":"normal"})") ||
      build_assets(pbr_request, registry).result != build_result::processor_failed) {
    return 13;
  }

  std::error_code error;
  std::filesystem::remove_all(root, error);
  return 0;
}
