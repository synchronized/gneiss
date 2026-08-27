// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.h"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace gneiss::tooling::asset_import {
namespace {

void configure(std::ostream& stream) {
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<float>::max_digits10);
}

[[nodiscard]] std::string uuid_for(std::size_t index) {
  std::ostringstream stream;
  stream << "00000000-0000-4000-8000-" << std::hex << std::setfill('0') << std::setw(12)
         << index + 1U;
  return stream.str();
}

[[nodiscard]] bool write_mesh(const import_ir_primitive& primitive,
                              const std::filesystem::path& path) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  configure(stream);
  stream << "{\n  \"format\": \"gneiss.mesh\",\n  \"version\": 3,\n"
            "  \"topology\": \"triangle_list\",\n  \"vertices\": [\n";
  for (std::size_t index = 0; index < primitive.indices.size(); ++index) {
    const auto& vertex = primitive.vertices[primitive.indices[index]];
    stream << "    [" << vertex.position[0] << ',' << vertex.position[1] << ','
           << vertex.position[2] << ']' << (index + 1U == primitive.indices.size() ? "\n" : ",\n");
  }
  stream << "  ],\n  \"uvs\": [\n";
  for (std::size_t index = 0; index < primitive.indices.size(); ++index) {
    const auto& vertex = primitive.vertices[primitive.indices[index]];
    stream << "    [" << vertex.texcoord[0] << ',' << vertex.texcoord[1] << ']'
           << (index + 1U == primitive.indices.size() ? "\n" : ",\n");
  }
  stream << "  ],\n  \"normals\": [\n";
  for (std::size_t index = 0; index < primitive.indices.size(); ++index) {
    const auto& vertex = primitive.vertices[primitive.indices[index]];
    stream << "    [" << vertex.normal[0] << ',' << vertex.normal[1] << ',' << vertex.normal[2]
           << ']' << (index + 1U == primitive.indices.size() ? "\n" : ",\n");
  }
  stream << "  ]\n}\n";
  return stream.good();
}

[[nodiscard]] bool write_material(const import_ir_material& material, std::size_t index,
                                  const std::filesystem::path& path) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  configure(stream);
  stream << "{\n  \"format\": \"gneiss.material\",\n  \"version\": "
         << (material.base_color_image_index ? 2 : 1) << ",\n  \"color\": ["
         << material.base_color[0] << ',' << material.base_color[1] << ',' << material.base_color[2]
         << ',' << material.base_color[3] << ']';
  if (material.base_color_image_index) {
    stream << ",\n  \"base_color_texture\": \"asset://textures/image-"
           << *material.base_color_image_index << ".texture.json\"";
  }
  stream << "\n}\n";
  (void)index;
  return stream.good();
}

[[nodiscard]] bool write_scene(const import_ir& data, const std::filesystem::path& path) {
  std::vector<std::optional<std::size_t>> parents(data.nodes.size());
  for (std::size_t parent = 0; parent < data.nodes.size(); ++parent) {
    for (const auto child : data.nodes[parent].children) {
      parents[child] = parent;
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  configure(stream);
  stream << "{\n  \"format\": \"gneiss.scene\",\n  \"version\": 1,\n  \"objects\": [\n";
  for (std::size_t index = 0; index < data.nodes.size(); ++index) {
    const auto& node = data.nodes[index];
    stream << R"(    {"uuid": ")" << uuid_for(index) << R"(", "parent": )";
    if (parents[index]) {
      stream << '\"' << uuid_for(*parents[index]) << '\"';
    } else {
      stream << "null";
    }
    stream << R"(, "transform": {"translation": [)" << node.translation[0] << ','
           << node.translation[1] << ',' << node.translation[2] << "], \"rotation\": ["
           << node.rotation[0] << ',' << node.rotation[1] << ',' << node.rotation[2] << ','
           << node.rotation[3] << "], \"scale\": [" << node.scale[0] << ',' << node.scale[1] << ','
           << node.scale[2] << "]}, \"components\": {";
    if (node.mesh_index) {
      const auto& primitive = data.meshes[*node.mesh_index].primitives[0];
      stream << R"("mesh_renderer": {"mesh": "asset://models/mesh-)" << *node.mesh_index
             << R"(.mesh.json", "material": "asset://materials/material-)"
             << primitive.material_index.value_or(0U) << ".material.json\"}";
    }
    stream << "}}" << (index + 1U == data.nodes.size() ? "\n" : ",\n");
  }
  stream << "  ]\n}\n";
  return stream.good();
}

} // namespace

write_report write_assets(const import_ir& data, const std::filesystem::path& output_directory) {
  try {
    if (output_directory.empty()) {
      return {.success = false, .diagnostic = "输出目录不能为空"};
    }
    std::filesystem::create_directories(output_directory / "models");
    std::filesystem::create_directories(output_directory / "materials");
    std::filesystem::create_directories(output_directory / "textures");
    std::filesystem::create_directories(output_directory / "scenes");
    for (std::size_t index = 0; index < data.meshes.size(); ++index) {
      if (data.meshes[index].primitives.size() != 1U ||
          !write_mesh(data.meshes[index].primitives[0],
                      output_directory / "models" /
                          ("mesh-" + std::to_string(index) + ".mesh.json"))) {
        return {.success = false, .diagnostic = "首版写出要求每个 Mesh 恰好包含一个 Primitive"};
      }
    }
    for (std::size_t index = 0; index < data.materials.size(); ++index) {
      if (!write_material(data.materials[index], index,
                          output_directory / "materials" /
                              ("material-" + std::to_string(index) + ".material.json"))) {
        return {.success = false, .diagnostic = "写出 Material 失败"};
      }
    }
    for (std::size_t index = 0; index < data.images.size(); ++index) {
      const auto base = "image-" + std::to_string(index);
      std::ofstream image(output_directory / "textures" / (base + ".png"),
                          std::ios::binary | std::ios::trunc);
      image.write(reinterpret_cast<const char*>(data.images[index].bytes.data()),
                  static_cast<std::streamsize>(data.images[index].bytes.size()));
      std::ofstream texture(output_directory / "textures" / (base + ".texture.json"),
                            std::ios::binary | std::ios::trunc);
      texture << "{\n  \"format\": \"gneiss.texture\",\n  \"version\": 1,\n"
                 "  \"source\": \"asset://textures/"
              << base << ".png\",\n  \"color_space\": \"srgb\"\n}\n";
      if (!image.good() || !texture.good()) {
        return {.success = false, .diagnostic = "写出 Texture 失败"};
      }
    }
    if (!write_scene(data, output_directory / "scenes" / "scene.scene.json")) {
      return {.success = false, .diagnostic = "写出 Scene 失败"};
    }
    return {.success = true, .diagnostic = {}};
  } catch (const std::exception& error) {
    return {.success = false, .diagnostic = std::string{"资产写出失败："} + error.what()};
  }
}

} // namespace gneiss::tooling::asset_import
