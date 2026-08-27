// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.h"
#include "tooling/asset_import/gltf_importer.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void print_usage() {
  std::cerr << "用法：\n"
               "  gneiss_assetc inspect <source.gltf|source.glb>\n"
               "  gneiss_assetc import <source.gltf|source.glb> --output <directory>\n";
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
  const bool inspect = argc == 3 && std::string_view{argv[1]} == "inspect";
  const bool import =
      argc == 5 && std::string_view{argv[1]} == "import" && std::string_view{argv[3]} == "--output";
  if (!inspect && !import) {
    print_usage();
    return 2;
  }

  const auto report = gneiss::tooling::asset_import::inspect_gltf(std::filesystem::path{argv[2]});
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
