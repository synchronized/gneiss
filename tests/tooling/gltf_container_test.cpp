// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void write_glb(const std::filesystem::path& path, std::string json) {
  while (json.size() % 4U != 0U) {
    json.push_back(' ');
  }
  const auto total_length = static_cast<std::uint32_t>(20U + json.size());
  const auto json_length = static_cast<std::uint32_t>(json.size());
  constexpr std::uint32_t magic = 0x46546C67U;
  constexpr std::uint32_t version = 2U;
  constexpr std::uint32_t json_chunk = 0x4E4F534AU;
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
  stream.write(reinterpret_cast<const char*>(&total_length), sizeof(total_length));
  stream.write(reinterpret_cast<const char*>(&json_length), sizeof(json_length));
  stream.write(reinterpret_cast<const char*>(&json_chunk), sizeof(json_chunk));
  stream.write(json.data(), static_cast<std::streamsize>(json.size()));
}

void write_embedded_image_glb(const std::filesystem::path& path) {
  constexpr std::array<float, 24> values = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,
                                            0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1};
  constexpr std::array<std::uint16_t, 3> indices = {0, 1, 2};
  constexpr std::array<std::uint8_t, 8> png_signature = {137, 80, 78, 71, 13, 10, 26, 10};
  std::string json =
      R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"textures":[{"source":0}],"images":[{"bufferView":4,"mimeType":"image/png"}],"buffers":[{"byteLength":112}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":102,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}]})";
  while (json.size() % 4U != 0U) {
    json.push_back(' ');
  }
  constexpr std::uint32_t binary_length = 112U;
  const auto total_length = static_cast<std::uint32_t>(28U + json.size() + binary_length);
  const auto json_length = static_cast<std::uint32_t>(json.size());
  constexpr std::uint32_t magic = 0x46546C67U;
  constexpr std::uint32_t version = 2U;
  constexpr std::uint32_t json_chunk = 0x4E4F534AU;
  constexpr std::uint32_t binary_chunk = 0x004E4942U;
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
  stream.write(reinterpret_cast<const char*>(&total_length), sizeof(total_length));
  stream.write(reinterpret_cast<const char*>(&json_length), sizeof(json_length));
  stream.write(reinterpret_cast<const char*>(&json_chunk), sizeof(json_chunk));
  stream.write(json.data(), static_cast<std::streamsize>(json.size()));
  stream.write(reinterpret_cast<const char*>(&binary_length), sizeof(binary_length));
  stream.write(reinterpret_cast<const char*>(&binary_chunk), sizeof(binary_chunk));
  stream.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(sizeof(values)));
  stream.write(reinterpret_cast<const char*>(indices.data()),
               static_cast<std::streamsize>(sizeof(indices)));
  stream.write(reinterpret_cast<const char*>(png_signature.data()),
               static_cast<std::streamsize>(png_signature.size()));
  constexpr std::array<std::uint8_t, 2> padding{};
  stream.write(reinterpret_cast<const char*>(padding.data()),
               static_cast<std::streamsize>(padding.size()));
}

void write_external_fixture(const std::filesystem::path& root) {
  constexpr std::array<float, 24> values = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,
                                            0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1};
  constexpr std::array<std::uint16_t, 3> indices = {0, 1, 2};
  std::ofstream binary(root / "external.bin", std::ios::binary | std::ios::trunc);
  binary.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(sizeof(values)));
  binary.write(reinterpret_cast<const char*>(indices.data()),
               static_cast<std::streamsize>(sizeof(indices)));
  std::ofstream json(root / "external.gltf", std::ios::binary | std::ios::trunc);
  json
      << R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],"buffers":[{"byteLength":102,"uri":"external.bin"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

void write_uri_fixture(const std::filesystem::path& path, std::string_view uri) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":")" << uri << R"("}]})";
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling::asset_import;
  const auto root = std::filesystem::temp_directory_path() / "gneiss-gltf-container-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  write_glb(root / "triangle.glb",
            read_file(std::filesystem::path{GNEISS_TEST_GLTF_ROOT} / "static_triangle.gltf"));
  write_embedded_image_glb(root / "embedded-image.glb");
  write_external_fixture(root);
  write_uri_fixture(root / "parent-escape.gltf", "../outside.bin");
  write_uri_fixture(root / "absolute.gltf", "/outside.bin");
  write_uri_fixture(root / "network.gltf", "https://example.invalid/outside.bin");
  write_uri_fixture(root / "encoded-escape.gltf", "%2e%2e/outside.bin");
  const auto outside = root.parent_path() / "gneiss-gltf-container-outside.bin";
  std::ofstream{outside, std::ios::binary | std::ios::trunc}.put('\0');
  std::error_code symlink_error;
  std::filesystem::create_symlink(outside, root / "linked.bin", symlink_error);
  if (!symlink_error) {
    write_uri_fixture(root / "symlink-escape.gltf", "linked.bin");
  }
  const auto glb = asset_import::inspect_gltf(root / "triangle.glb");
  const auto embedded_image = asset_import::inspect_gltf(root / "embedded-image.glb");
  const auto external = asset_import::inspect_gltf(root / "external.gltf");
  const std::array unsafe_sources{root / "parent-escape.gltf", root / "absolute.gltf",
                                  root / "network.gltf", root / "encoded-escape.gltf"};
  if (glb.result != asset_import::inspect_result::success) {
    std::cerr << "GLB：" << glb.diagnostic << '\n';
  }
  if (external.result != asset_import::inspect_result::success) {
    std::cerr << "外部 Buffer：" << external.diagnostic << '\n';
  }
  if (glb.result != asset_import::inspect_result::success || glb.summary.primitive_count != 1U ||
      embedded_image.result != asset_import::inspect_result::success ||
      embedded_image.data.images.size() != 1U || embedded_image.data.images[0].bytes.size() != 8U ||
      external.result != asset_import::inspect_result::success ||
      external.data.meshes[0].primitives[0].vertices.size() != 3U) {
    return 1;
  }
  for (const auto& source : unsafe_sources) {
    if (asset_import::inspect_gltf(source).result != asset_import::inspect_result::invalid_source) {
      return 2;
    }
  }
  if (!symlink_error && asset_import::inspect_gltf(root / "symlink-escape.gltf").result !=
                            asset_import::inspect_result::invalid_source) {
    return 3;
  }
  std::filesystem::remove_all(root);
  std::filesystem::remove(outside);
  return 0;
}
