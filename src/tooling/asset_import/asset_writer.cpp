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

[[nodiscard]] std::string mesh_name(std::size_t mesh, std::size_t primitive) {
  return "mesh-" + std::to_string(mesh) + "-primitive-" + std::to_string(primitive) + ".mesh.json";
}

[[nodiscard]] std::string material_name(const import_ir_primitive& primitive) {
  return primitive.material_index
             ? "material-" + std::to_string(*primitive.material_index) + ".material.json"
             : "default.material.json";
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

[[nodiscard]] bool write_material(const import_ir_material& material,
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
  return stream.good();
}

void write_transform(std::ostream& stream, const std::array<float, 3>& translation,
                     const std::array<float, 4>& rotation, const std::array<float, 3>& scale) {
  stream << R"("transform": {"translation": [)" << translation[0] << ',' << translation[1] << ','
         << translation[2] << R"(], "rotation": [)" << rotation[0] << ',' << rotation[1] << ','
         << rotation[2] << ',' << rotation[3] << R"(], "scale": [)" << scale[0] << ',' << scale[1]
         << ',' << scale[2] << "]}";
}

void write_renderer(std::ostream& stream, std::size_t mesh_index, std::size_t primitive_index,
                    const import_ir_primitive& primitive) {
  stream << R"("mesh_renderer": {"mesh": "asset://models/)"
         << mesh_name(mesh_index, primitive_index) << R"(", "material": "asset://materials/)"
         << material_name(primitive) << R"("})";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 输出顺序直接对应稳定 Scene Schema。
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
  stream << "{\n  \"format\": \"gneiss.scene\",\n  \"version\": 1,\n"
            "  \"scene_uuid\": \"00000000-0000-4000-8000-000000000000\",\n"
            "  \"objects\": [\n";
  std::size_t synthetic_count = 0;
  for (const auto& node : data.nodes) {
    if (node.mesh_index && data.meshes[*node.mesh_index].primitives.size() > 1U) {
      synthetic_count += data.meshes[*node.mesh_index].primitives.size();
    }
  }
  const auto total_count = data.nodes.size() + synthetic_count;
  std::size_t emitted = 0;
  for (std::size_t index = 0; index < data.nodes.size(); ++index) {
    const auto& node = data.nodes[index];
    stream << R"(    {"uuid": ")" << uuid_for(index) << R"(", "parent": )";
    if (parents[index]) {
      stream << '\"' << uuid_for(*parents[index]) << '\"';
    } else {
      stream << "null";
    }
    stream << ", ";
    write_transform(stream, node.translation, node.rotation, node.scale);
    stream << R"(, "components": {)";
    if (node.mesh_index && data.meshes[*node.mesh_index].primitives.size() == 1U) {
      write_renderer(stream, *node.mesh_index, 0U, data.meshes[*node.mesh_index].primitives[0]);
    }
    stream << "}}" << (++emitted == total_count ? "\n" : ",\n");
  }
  std::size_t synthetic_index = 0;
  constexpr std::array<float, 3> identity_translation{};
  constexpr std::array<float, 4> identity_rotation{0.0F, 0.0F, 0.0F, 1.0F};
  constexpr std::array<float, 3> identity_scale{1.0F, 1.0F, 1.0F};
  for (std::size_t node_index = 0; node_index < data.nodes.size(); ++node_index) {
    const auto& node = data.nodes[node_index];
    if (!node.mesh_index || data.meshes[*node.mesh_index].primitives.size() <= 1U) {
      continue;
    }
    const auto& primitives = data.meshes[*node.mesh_index].primitives;
    for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
      stream << R"(    {"uuid": ")" << uuid_for(data.nodes.size() + synthetic_index++)
             << R"(", "parent": ")" << uuid_for(node_index) << R"(", )";
      write_transform(stream, identity_translation, identity_rotation, identity_scale);
      stream << R"(, "components": {)";
      write_renderer(stream, *node.mesh_index, primitive_index, primitives[primitive_index]);
      stream << "}}" << (++emitted == total_count ? "\n" : ",\n");
    }
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
    for (std::size_t mesh_index = 0; mesh_index < data.meshes.size(); ++mesh_index) {
      for (std::size_t primitive_index = 0;
           primitive_index < data.meshes[mesh_index].primitives.size(); ++primitive_index) {
        if (!write_mesh(data.meshes[mesh_index].primitives[primitive_index],
                        output_directory / "models" / mesh_name(mesh_index, primitive_index))) {
          return {.success = false, .diagnostic = "写出 Mesh 失败"};
        }
      }
    }
    for (std::size_t index = 0; index < data.materials.size(); ++index) {
      if (!write_material(data.materials[index],
                          output_directory / "materials" /
                              ("material-" + std::to_string(index) + ".material.json"))) {
        return {.success = false, .diagnostic = "写出 Material 失败"};
      }
    }
    bool needs_default = false;
    for (const auto& mesh : data.meshes) {
      for (const auto& primitive : mesh.primitives) {
        needs_default = needs_default || !primitive.material_index;
      }
    }
    if (needs_default && !write_material(import_ir_material{}, output_directory / "materials" /
                                                                   "default.material.json")) {
      return {.success = false, .diagnostic = "写出默认 Material 失败"};
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
