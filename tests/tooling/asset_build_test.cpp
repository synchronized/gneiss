// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/asset_build.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

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
      !std::filesystem::exists(development.output_root / "textures/a.ktx2") ||
      std::filesystem::exists(development.output_root / "textures/a.png")) {
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
      std::filesystem::exists(shipping_request.output_root / "textures/unused.ktx2")) {
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

  std::error_code error;
  std::filesystem::remove_all(root, error);
  return 0;
}
