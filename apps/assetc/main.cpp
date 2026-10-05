// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/asset_build.hpp"
#include "tooling/asset_build/ktx2_probe.hpp"
#include "tooling/asset_build/runtime_texture_probe.hpp"
#include "tooling/asset_import/asset_writer.hpp"
#include "tooling/asset_import/gltf_importer.hpp"

#include "engine/asset/mesh_binary.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void print_usage() {
  std::cerr << "用法：\n"
               "  gneiss_assetc inspect <source.gltf|source.glb>\n"
               "  gneiss_assetc inspect <mesh.gneiss-mesh>\n"
               "  gneiss_assetc inspect <texture.ktx2>\n"
               "  gneiss_assetc inspect <texture.gneiss-texture>\n"
               "  gneiss_assetc validate <texture.gneiss-texture>\n"
               "  gneiss_assetc validate <mesh.gneiss-mesh>\n"
               "  gneiss_assetc dump <mesh.gneiss-mesh> --format json\n"
               "  gneiss_assetc import <source.gltf|source.glb> --output <directory>\n"
               "  gneiss_assetc cook <asset-directory> --output <directory> --cache <directory>\n";
}

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return {};
  }
  const auto size = stream.tellg();
  if (size <= 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()), size);
  return stream.good() ? std::move(bytes) : std::vector<std::byte>{};
}

int process_binary_mesh(std::string_view command, const std::filesystem::path& path) {
  const auto bytes = read_file(path);
  gneiss::asset_internal::mesh_binary_data data;
  gneiss::asset_internal::mesh_binary_diagnostic diagnostic;
  if (gneiss::asset_internal::decode_mesh_binary(bytes, data, diagnostic) !=
      gneiss::asset_internal::mesh_binary_result::success) {
    std::cerr << diagnostic.message << "（字节 " << diagnostic.byte_offset << "）\n";
    return 1;
  }
  if (command == "inspect") {
    std::cout << "Mesh Binary v1 顶点=" << data.vertices.size() << " 索引=" << data.indices.size()
              << " 三角形=" << data.indices.size() / 3U << " 字节=" << bytes.size() << '\n';
  } else if (command == "dump") {
    std::cout << gneiss::asset_internal::dump_mesh_binary_json(data);
  } else {
    std::cout << "Mesh Binary 有效\n";
  }
  return 0;
}

int inspect_ktx2(const std::filesystem::path& path) {
  const auto report = gneiss::tooling::asset_build::inspect_ktx2(path);
  if (report.result != gneiss::tooling::asset_build::ktx2_probe_result::success) {
    std::cerr << report.diagnostic << '\n';
    return 1;
  }
  const auto& information = report.information;
  std::cout << "KTX2 " << information.width << 'x' << information.height
            << " Mip=" << information.level_count << " VkFormat=" << information.vk_format
            << " Alpha=" << (information.has_alpha ? "是" : "否") << '\n';
  return 0;
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
  const bool inspect = argc == 3 && std::string_view{argv[1]} == "inspect";
  const bool import =
      argc == 5 && std::string_view{argv[1]} == "import" && std::string_view{argv[3]} == "--output";
  const bool validate = argc == 3 && std::string_view{argv[1]} == "validate";
  const bool dump = argc == 5 && std::string_view{argv[1]} == "dump" &&
                    std::string_view{argv[3]} == "--format" && std::string_view{argv[4]} == "json";
  const bool cook = argc == 7 && std::string_view{argv[1]} == "cook" &&
                    std::string_view{argv[3]} == "--output" &&
                    std::string_view{argv[5]} == "--cache";
  if (!inspect && !import && !validate && !dump && !cook) {
    print_usage();
    return 2;
  }

  const auto source = std::filesystem::path{argv[2]};
  if (cook) {
    const auto registry = gneiss::tooling::asset_build::make_default_registry();
    const auto report = gneiss::tooling::asset_build::build_assets(
        {.source_root = source,
         .output_root = std::filesystem::path{argv[4]},
         .cache_root = std::filesystem::path{argv[6]},
         .root_uris = {},
#if defined(_WIN32)
         .target_platform = "windows",
#elif defined(__APPLE__)
         .target_platform = "macos",
#else
         .target_platform = "linux",
#endif
#if defined(_M_X64) || defined(__x86_64__)
         .target_architecture = "x86_64",
#elif defined(_M_ARM64) || defined(__aarch64__)
         .target_architecture = "arm64",
#else
         .target_architecture = "unknown",
#endif
         .profile = gneiss::tooling::asset_build::build_profile::development,
         .progress = {}},
        registry);
    if (report.result != gneiss::tooling::asset_build::build_result::success) {
      std::cerr << report.diagnostic << '\n';
      return 1;
    }
    std::cout << "资产构建完成：输出=" << report.outputs.size() << " 构建=" << report.built_count
              << " 缓存命中=" << report.cache_hit_count << '\n';
    return 0;
  }
  if ((inspect || validate) && source.extension() == ".gneiss-texture") {
    std::string summary;
    std::string diagnostic;
    if (!gneiss::tooling::asset_build::inspect_runtime_texture(read_file(source), summary,
                                                               diagnostic)) {
      std::cerr << (diagnostic.empty() ? "运行纹理检查失败" : diagnostic) << '\n';
      return 1;
    }
    std::cout << (inspect ? summary : "Texture Binary 有效（全部变体摘要已校验）\n");
    return 0;
  }
  if (validate || dump || (inspect && source.extension() == ".gneiss-mesh")) {
    return process_binary_mesh(argv[1], source);
  }
  if (inspect && source.extension() == ".ktx2") {
    return inspect_ktx2(source);
  }

  const auto report = gneiss::tooling::asset_import::inspect_gltf(source);
  if (report.result != gneiss::tooling::asset_import::inspect_result::success) {
    std::cerr << report.diagnostic << '\n';
    return 1;
  }

  if (!report.diagnostic.empty())
    std::cerr << report.diagnostic << '\n';
  if (import) {
    const auto written =
        gneiss::tooling::asset_import::write_assets(report.data, std::filesystem::path{argv[4]});
    if (!written.success) {
      std::cerr << written.diagnostic << '\n';
      return 1;
    }
    std::cout << "资产已写入 " << argv[4] << '\n';
    return 0;
  }

  std::cout << "场景=" << report.summary.scene_count << " 节点=" << report.summary.node_count
            << " 网格=" << report.summary.mesh_count << " 图元=" << report.summary.primitive_count
            << " 材质=" << report.summary.material_count << " 图像=" << report.summary.image_count
            << '\n';
  return 0;
}
