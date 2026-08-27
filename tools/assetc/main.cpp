// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void print_usage() { std::cerr << "用法：gneiss_assetc inspect <source.gltf|source.glb>\n"; }

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
  if (argc != 3 || std::string_view{argv[1]} != "inspect") {
    print_usage();
    return 2;
  }

  const auto report = gneiss::tooling::asset_import::inspect_gltf(std::filesystem::path{argv[2]});
  if (report.result != gneiss::tooling::asset_import::inspect_result::success) {
    std::cerr << report.diagnostic << '\n';
    return 1;
  }

  std::cout << "场景=" << report.summary.scene_count << " 节点=" << report.summary.node_count
            << " 网格=" << report.summary.mesh_count << " 图元=" << report.summary.primitive_count
            << " 材质=" << report.summary.material_count << " 图像=" << report.summary.image_count
            << '\n';
  return 0;
}
