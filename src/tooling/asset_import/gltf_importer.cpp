// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <exception>
#include <string>
#include <string_view>

namespace gneiss::tooling::asset_import {
namespace {

[[nodiscard]] inspect_report failure(inspect_result result, std::string diagnostic) {
  inspect_report report;
  report.result = result;
  report.diagnostic = std::move(diagnostic);
  return report;
}

[[nodiscard]] std::string read_failure_detail(std::string_view detail) {
  std::string message{"无法读取 glTF："};
  message.append(detail);
  return message;
}

[[nodiscard]] std::string parse_failure_detail(std::string_view detail) {
  std::string message{"glTF 解析失败："};
  message.append(detail);
  return message;
}

} // namespace

inspect_report inspect_gltf(const std::filesystem::path& source_path) {
  if (source_path.empty()) {
    return failure(inspect_result::invalid_argument, "源文件路径不能为空");
  }

  try {
    auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
    if (!source) {
      return failure(inspect_result::source_unavailable,
                     read_failure_detail(fastgltf::getErrorMessage(source.error())));
    }

    fastgltf::Parser parser;
    constexpr auto options =
        fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;
    auto loaded = parser.loadGltf(source.get(), source_path.parent_path(), options);
    if (loaded.error() != fastgltf::Error::None) {
      return failure(inspect_result::invalid_source,
                     parse_failure_detail(fastgltf::getErrorMessage(loaded.error())));
    }

    auto& asset = loaded.get();
    if (!asset.extensionsRequired.empty()) {
      return failure(inspect_result::unsupported_feature, "首版不支持 glTF 必需扩展");
    }
    if (!asset.animations.empty() || !asset.skins.empty()) {
      return failure(inspect_result::unsupported_feature, "首版只支持静态场景");
    }

    std::size_t primitive_count = 0;
    for (const auto& mesh : asset.meshes) {
      for (const auto& primitive : mesh.primitives) {
        ++primitive_count;
        if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty() ||
            primitive.dracoCompression != nullptr) {
          return failure(inspect_result::unsupported_feature,
                         "首版只支持未压缩的静态三角形 Primitive");
        }
        if (primitive.findAttribute("POSITION") == primitive.attributes.end() ||
            primitive.findAttribute("NORMAL") == primitive.attributes.end() ||
            primitive.findAttribute("TEXCOORD_0") == primitive.attributes.end()) {
          return failure(inspect_result::unsupported_feature,
                         "Primitive 必须包含 POSITION、NORMAL 和 TEXCOORD_0");
        }
      }
    }

    inspect_report report;
    report.result = inspect_result::success;
    report.summary.scene_count = asset.scenes.size();
    report.summary.node_count = asset.nodes.size();
    report.summary.mesh_count = asset.meshes.size();
    report.summary.primitive_count = primitive_count;
    report.summary.material_count = asset.materials.size();
    report.summary.image_count = asset.images.size();
    return report;
  } catch (const std::exception& error) {
    return failure(inspect_result::invalid_source,
                   std::string{"glTF 检查发生异常："} + error.what());
  } catch (...) {
    return failure(inspect_result::invalid_source, "glTF 检查发生未知异常");
  }
}

} // namespace gneiss::tooling::asset_import
