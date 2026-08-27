// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <exception>
#include <limits>
#include <optional>
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

[[nodiscard]] bool checked_add(std::size_t left, std::size_t right, std::size_t& result) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool accessors_fit_buffers(const fastgltf::Asset& asset) {
  for (const auto& view : asset.bufferViews) {
    std::size_t view_end = 0;
    if (view.bufferIndex >= asset.buffers.size() ||
        !checked_add(view.byteOffset, view.byteLength, view_end) ||
        view_end > asset.buffers[view.bufferIndex].byteLength) {
      return false;
    }
  }

  for (const auto& accessor : asset.accessors) {
    if (!accessor.bufferViewIndex) {
      continue;
    }
    if (*accessor.bufferViewIndex >= asset.bufferViews.size()) {
      return false;
    }
    const auto& view = asset.bufferViews[*accessor.bufferViewIndex];
    const auto element_size = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
    const auto stride = view.byteStride.value_or(element_size);
    if (stride < element_size) {
      return false;
    }
    std::size_t last_offset = accessor.byteOffset;
    if (accessor.count > 0U &&
        ((accessor.count - 1U) > (std::numeric_limits<std::size_t>::max() - last_offset) / stride ||
         !checked_add(last_offset, (accessor.count - 1U) * stride, last_offset) ||
         !checked_add(last_offset, element_size, last_offset))) {
      return false;
    }
    if (last_offset > view.byteLength) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::string> validate_supported_scope(const fastgltf::Asset& asset,
                                                                  std::size_t& primitive_count) {
  if (!asset.extensionsRequired.empty()) {
    return "首版不支持 glTF 必需扩展";
  }
  if (!asset.animations.empty() || !asset.skins.empty()) {
    return "首版只支持静态场景";
  }
  for (const auto& mesh : asset.meshes) {
    for (const auto& primitive : mesh.primitives) {
      ++primitive_count;
      if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty() ||
          primitive.dracoCompression != nullptr) {
        return "首版只支持未压缩的静态三角形 Primitive";
      }
      if (primitive.findAttribute("POSITION") == primitive.attributes.end() ||
          primitive.findAttribute("NORMAL") == primitive.attributes.end() ||
          primitive.findAttribute("TEXCOORD_0") == primitive.attributes.end()) {
        return "Primitive 必须包含 POSITION、NORMAL 和 TEXCOORD_0";
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] import_ir build_import_ir(const fastgltf::Asset& asset) {
  import_ir result;
  result.nodes.reserve(asset.nodes.size());
  for (const auto& node : asset.nodes) {
    import_ir_node ir_node;
    ir_node.name.assign(node.name.begin(), node.name.end());
    if (node.meshIndex) {
      ir_node.mesh_index = *node.meshIndex;
    }
    ir_node.children.assign(node.children.begin(), node.children.end());
    result.nodes.push_back(std::move(ir_node));
  }

  result.meshes.reserve(asset.meshes.size());
  for (const auto& mesh : asset.meshes) {
    import_ir_mesh ir_mesh;
    ir_mesh.name.assign(mesh.name.begin(), mesh.name.end());
    ir_mesh.primitives.reserve(mesh.primitives.size());
    for (const auto& primitive : mesh.primitives) {
      const auto* const position = primitive.findAttribute("POSITION");
      const auto* const normal = primitive.findAttribute("NORMAL");
      const auto* const texcoord = primitive.findAttribute("TEXCOORD_0");
      import_ir_primitive ir_primitive;
      ir_primitive.position_accessor = position->accessorIndex;
      ir_primitive.normal_accessor = normal->accessorIndex;
      ir_primitive.texcoord_accessor = texcoord->accessorIndex;
      if (primitive.indicesAccessor) {
        ir_primitive.index_accessor = *primitive.indicesAccessor;
      }
      if (primitive.materialIndex) {
        ir_primitive.material_index = *primitive.materialIndex;
      }
      ir_mesh.primitives.push_back(ir_primitive);
    }
    result.meshes.push_back(std::move(ir_mesh));
  }

  result.materials.reserve(asset.materials.size());
  for (const auto& material : asset.materials) {
    import_ir_material ir_material;
    ir_material.name.assign(material.name.begin(), material.name.end());
    result.materials.push_back(std::move(ir_material));
  }
  return result;
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
    if (const auto validation = fastgltf::validate(asset); validation != fastgltf::Error::None) {
      return failure(inspect_result::invalid_source,
                     parse_failure_detail(fastgltf::getErrorMessage(validation)));
    }
    if (!accessors_fit_buffers(asset)) {
      return failure(inspect_result::invalid_source, "Accessor 或 BufferView 超出 Buffer 边界");
    }
    std::size_t primitive_count = 0;
    if (auto unsupported = validate_supported_scope(asset, primitive_count)) {
      return failure(inspect_result::unsupported_feature, std::move(*unsupported));
    }

    inspect_report report;
    report.result = inspect_result::success;
    report.summary.scene_count = asset.scenes.size();
    report.summary.node_count = asset.nodes.size();
    report.summary.mesh_count = asset.meshes.size();
    report.summary.primitive_count = primitive_count;
    report.summary.material_count = asset.materials.size();
    report.summary.image_count = asset.images.size();

    report.data = build_import_ir(asset);
    return report;
  } catch (const std::exception& error) {
    return failure(inspect_result::invalid_source,
                   std::string{"glTF 检查发生异常："} + error.what());
  } catch (...) {
    return failure(inspect_result::invalid_source, "glTF 检查发生未知异常");
  }
}

} // namespace gneiss::tooling::asset_import
