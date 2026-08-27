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

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling::asset_import;
  const auto root = std::filesystem::temp_directory_path() / "gneiss-gltf-container-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  write_glb(root / "triangle.glb",
            read_file(std::filesystem::path{GNEISS_TEST_GLTF_ROOT} / "static_triangle.gltf"));
  write_external_fixture(root);
  const auto glb = asset_import::inspect_gltf(root / "triangle.glb");
  const auto external = asset_import::inspect_gltf(root / "external.gltf");
  if (glb.result != asset_import::inspect_result::success) {
    std::cerr << "GLB：" << glb.diagnostic << '\n';
  }
  if (external.result != asset_import::inspect_result::success) {
    std::cerr << "外部 Buffer：" << external.diagnostic << '\n';
  }
  if (glb.result != asset_import::inspect_result::success || glb.summary.primitive_count != 1U ||
      external.result != asset_import::inspect_result::success ||
      external.data.meshes[0].primitives[0].vertices.size() != 3U) {
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
