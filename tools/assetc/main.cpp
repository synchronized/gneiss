// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.h"
#include "tooling/asset_import/gltf_importer.h"

#include "asset/mesh_binary.h"

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
               "  gneiss_assetc validate <mesh.gneiss-mesh>\n"
               "  gneiss_assetc dump <mesh.gneiss-mesh> --format json\n"
               "  gneiss_assetc import <source.gltf|source.glb> --output <directory>\n";
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

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
  const bool inspect = argc == 3 && std::string_view{argv[1]} == "inspect";
  const bool import =
      argc == 5 && std::string_view{argv[1]} == "import" && std::string_view{argv[3]} == "--output";
  const bool validate = argc == 3 && std::string_view{argv[1]} == "validate";
  const bool dump = argc == 5 && std::string_view{argv[1]} == "dump" &&
                    std::string_view{argv[3]} == "--format" && std::string_view{argv[4]} == "json";
  if (!inspect && !import && !validate && !dump) {
    print_usage();
    return 2;
  }

  const auto source = std::filesystem::path{argv[2]};
  if (validate || dump || (inspect && source.extension() == ".gneiss-mesh")) {
    return process_binary_mesh(argv[1], source);
  }

  const auto report = gneiss::tooling::asset_import::inspect_gltf(source);
  if (report.result != gneiss::tooling::asset_import::inspect_result::success) {
    std::cerr << report.diagnostic << '\n';
    return 1;
  }

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
