// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include "engine/resource/mesh/mesh_binary.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

namespace gneiss::tooling {
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
  return "mesh-" + std::to_string(mesh) + "-primitive-" + std::to_string(primitive) +
         ".gneiss-mesh";
}

[[nodiscard]] std::string material_name(const import_ir_primitive& primitive) {
  return primitive.material_index
             ? "material-" + std::to_string(*primitive.material_index) + ".material.json"
             : "default.material.json";
}

[[nodiscard]] bool write_mesh(const import_ir_primitive& primitive,
                              const std::filesystem::path& path) {
  gneiss::engine::mesh_binary_data data;
  data.vertices.reserve(primitive.vertices.size());
  data.tangents.reserve(primitive.vertices.size());
  data.indices = primitive.indices;
  for (const auto& source : primitive.vertices) {
    data.tangents.push_back(
        {source.tangent[0], source.tangent[1], source.tangent[2], source.tangent[3]});
    if (primitive.has_uv1)
      data.uv1.push_back({source.uv1[0], source.uv1[1]});
    if (primitive.has_color)
      data.colors.push_back({source.color[0], source.color[1], source.color[2], source.color[3]});
    data.vertices.push_back(
        {.position = {source.position[0], source.position[1], source.position[2]},
         .texcoord = {source.texcoord[0], source.texcoord[1]},
         .normal = {source.normal[0], source.normal[1], source.normal[2]}});
  }
  auto bytes = gneiss::engine::encode_mesh_binary(data);
  if (!bytes) {
    return false;
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  stream.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
  return stream.good();
}

[[nodiscard]] bool write_material(const import_ir_material& material,
                                  const std::filesystem::path& path,
                                  std::string_view asset_uri_prefix) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  configure(stream);
  // 离线 SDK 不链接 Engine；输出与原生材质描述一致的 v6 结构。
  stream << R"({"format":"gneiss.material","version":6,"parameters":{"color":[)"
         << material.base_color[0] << ',' << material.base_color[1] << ',' << material.base_color[2]
         << ',' << material.base_color[3] << ']' << R"(,"metallic":)" << material.metallic
         << R"(,"roughness":)" << material.roughness << R"(,"normal_scale":)"
         << material.normal_scale << R"(,"occlusion_strength":)" << material.occlusion_strength
         << R"(,"emissive":[)" << material.emissive[0] << ',' << material.emissive[1] << ','
         << material.emissive[2] << R"(],"alpha_mode":)"
         << static_cast<std::uint32_t>(material.alpha_mode) << R"(,"double_sided":)"
         << (material.double_sided ? "true" : "false") << R"(,"alpha_cutoff":)"
         << material.alpha_cutoff << R"(,"sampling":[)";
  for (std::size_t slot = 0; slot < material.sampling.size(); ++slot) {
    const auto& v = material.sampling[slot];
    if (slot)
      stream << ',';
    stream << R"({"uv_set":)" << v.uv_set << R"(,"mag_filter":)"
           << static_cast<std::uint32_t>(v.mag_filter) << R"(,"min_filter":)"
           << static_cast<std::uint32_t>(v.min_filter) << R"(,"mip_filter":)"
           << static_cast<std::uint32_t>(v.mip_filter) << R"(,"address_u":)"
           << static_cast<std::uint32_t>(v.address_u) << R"(,"address_v":)"
           << static_cast<std::uint32_t>(v.address_v) << '}';
  }
  stream << R"(]},"texture_uris":[)";
  constexpr std::array variants{0U, 1U, 2U, 1U, 0U};
  const auto images = material.texture_indices();
  for (std::size_t slot = 0; slot < images.size(); ++slot) {
    if (slot)
      stream << ',';
    stream << '"';
    if (images[slot])
      stream << asset_uri_prefix << "textures/image-" << *images[slot]
             << image_variant_suffixes[variants[slot]] << ".texture.json";
    stream << '"';
  }
  stream << "]}\n";
  return stream.good();
}

void write_transform(std::ostream& stream, const gneiss::engine::math::vector3& translation,
                     const gneiss::engine::math::quaternion& rotation,
                     const gneiss::engine::math::vector3& scale) {
  stream << R"("transform": {"translation": [)" << translation[0] << ',' << translation[1] << ','
         << translation[2] << R"(], "rotation": [)" << rotation[0] << ',' << rotation[1] << ','
         << rotation[2] << ',' << rotation[3] << R"(], "scale": [)" << scale[0] << ',' << scale[1]
         << ',' << scale[2] << "]}";
}

void write_renderer(std::ostream& stream, std::size_t mesh_index, std::size_t primitive_index,
                    const import_ir_primitive& primitive, std::string_view asset_uri_prefix) {
  stream << R"({"$type_guid": ")";
  // 仅使用 constexpr 身份值；离线 SDK 不依赖引擎 DLL 的注册表或 GUID 函数。
  constexpr auto bytes = gneiss::engine::mesh_renderer_description::type_guid.bytes();
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10)
      stream << '-';
    stream << hex[bytes[index] >> 4] << hex[bytes[index] & 15];
  }
  stream << R"(", "$content": {"mesh": ")" << asset_uri_prefix << "models/"
         << mesh_name(mesh_index, primitive_index) << R"(", "material": ")" << asset_uri_prefix
         << "materials/" << material_name(primitive) << R"("}})";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 输出顺序直接对应稳定 Scene Schema。
[[nodiscard]] bool write_scene(const import_ir& data, const std::filesystem::path& path,
                               std::string_view asset_uri_prefix) {
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
  stream << "{\n  \"format\": \"gneiss.scene\",\n  \"version\": 5,\n"
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
    stream << R"(, "components": [)";
    if (node.mesh_index && data.meshes[*node.mesh_index].primitives.size() == 1U) {
      write_renderer(stream, *node.mesh_index, 0U, data.meshes[*node.mesh_index].primitives[0],
                     asset_uri_prefix);
    }
    stream << "]}" << (++emitted == total_count ? "\n" : ",\n");
  }
  std::size_t synthetic_index = 0;
  const gneiss::engine::math::vector3 identity_translation{};
  const gneiss::engine::math::quaternion identity_rotation{1.0F, 0.0F, 0.0F, 0.0F};
  const gneiss::engine::math::vector3 identity_scale{1.0F};
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
      stream << R"(, "components": [)";
      write_renderer(stream, *node.mesh_index, primitive_index, primitives[primitive_index],
                     asset_uri_prefix);
      stream << "]}" << (++emitted == total_count ? "\n" : ",\n");
    }
  }
  stream << "  ]\n}\n";
  return stream.good();
}

[[nodiscard]] std::optional<std::string> validate_nodes(const import_ir& data) {
  for (const auto& node : data.nodes) {
    if (node.mesh_index && *node.mesh_index >= data.meshes.size()) {
      return "节点引用了不存在的 Mesh";
    }
    for (const auto child : node.children) {
      if (child >= data.nodes.size()) {
        return "节点引用了不存在的子节点";
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> validate_meshes(const import_ir& data) {
  for (const auto& mesh : data.meshes) {
    for (const auto& primitive : mesh.primitives) {
      if (primitive.material_index && *primitive.material_index >= data.materials.size()) {
        return "Primitive 引用了不存在的 Material";
      }
      for (const auto index : primitive.indices) {
        if (index >= primitive.vertices.size()) {
          return "Primitive 索引超出顶点范围";
        }
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> validate_materials(const import_ir& data) {
  for (const auto& material : data.materials) {
    for (const auto image : material.texture_indices()) {
      if (image && *image >= data.images.size()) {
        return "Material 引用了不存在的图像";
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> validate(const import_ir& data) {
  if (auto diagnostic = validate_nodes(data)) {
    return diagnostic;
  }
  if (auto diagnostic = validate_meshes(data)) {
    return diagnostic;
  }
  return validate_materials(data);
}

write_report write_assets_in_place(const import_ir& data,
                                   const std::filesystem::path& output_directory,
                                   std::string_view asset_uri_prefix) {
  try {
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
                              ("material-" + std::to_string(index) + ".material.json"),
                          asset_uri_prefix)) {
        return {.success = false, .diagnostic = "写出 Material 失败"};
      }
    }
    bool needs_default = false;
    for (const auto& mesh : data.meshes) {
      for (const auto& primitive : mesh.primitives) {
        needs_default = needs_default || !primitive.material_index;
      }
    }
    if (needs_default && !write_material(import_ir_material{},
                                         output_directory / "materials" / "default.material.json",
                                         asset_uri_prefix)) {
      return {.success = false, .diagnostic = "写出默认 Material 失败"};
    }
    for (std::size_t index = 0; index < data.images.size(); ++index) {
      const auto variants = image_variants(data, index);
      for (std::size_t variant = 0; variant < variants.size(); ++variant) {
        if (!variants[variant])
          continue;
        const auto base = "image-" + std::to_string(index) + image_variant_suffixes[variant];
        std::ofstream image(output_directory / "textures" / (base + ".png"),
                            std::ios::binary | std::ios::trunc);
        image.write(reinterpret_cast<const char*>(data.images[index].bytes.data()),
                    static_cast<std::streamsize>(data.images[index].bytes.size()));
        std::ofstream texture(output_directory / "textures" / (base + ".texture.json"),
                              std::ios::binary | std::ios::trunc);
        texture << "{\n  \"format\": \"gneiss.texture\",\n  \"version\": " << 2
                << ",\n  \"source\": \"" << asset_uri_prefix << "textures/" << base
                << ".png\",\n  \"color_space\": \"" << (variant == 0U ? "srgb" : "linear") << '"';
        texture << ",\n  \"usage\": \"" << (variant == 2U ? "normal" : "default") << '"';
        texture << "\n}\n";
        if (!image.good() || !texture.good())
          return {.success = false, .diagnostic = "写出 Texture 失败"};
      }
    }
    if (!write_scene(data, output_directory / "scenes" / "scene.scene.json", asset_uri_prefix)) {
      return {.success = false, .diagnostic = "写出 Scene 失败"};
    }
    return {.success = true, .diagnostic = {}};
  } catch (const std::exception& error) {
    return {.success = false, .diagnostic = std::string{"资产写出失败："} + error.what()};
  }
}

bool remove_quietly(const std::filesystem::path& path) noexcept {
  try {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    return !error;
  } catch (...) {
    // 清理是尽力而为；调用方的主错误和恢复流程优先。
    return false;
  }
}

} // namespace

write_report write_assets(const import_ir& data, const std::filesystem::path& output_directory,
                          std::string_view asset_uri_prefix) {
  if (output_directory.empty()) {
    return {.success = false, .diagnostic = "输出目录不能为空"};
  }
  if (const auto diagnostic = validate(data)) {
    return {.success = false, .diagnostic = *diagnostic};
  }

  std::filesystem::path destination;
  std::filesystem::path staging;
  std::filesystem::path backup;
  bool old_output_moved = false;
  try {
    destination = std::filesystem::absolute(output_directory).lexically_normal();
    if (destination == destination.root_path() || destination.filename().empty()) {
      return {.success = false, .diagnostic = "输出目录不能是文件系统根目录"};
    }
    staging = destination.parent_path() / (destination.filename().string() + ".gneiss-staging");
    backup = destination.parent_path() / (destination.filename().string() + ".gneiss-backup");

    remove_quietly(staging);
    if (std::filesystem::exists(backup)) {
      if (std::filesystem::exists(destination)) {
        remove_quietly(backup);
      } else {
        std::filesystem::rename(backup, destination);
      }
    }

    auto report = write_assets_in_place(data, staging, asset_uri_prefix);
    if (!report.success) {
      remove_quietly(staging);
      return report;
    }
    if (std::filesystem::exists(destination)) {
      std::filesystem::rename(destination, backup);
      old_output_moved = true;
    }
    std::filesystem::rename(staging, destination);
    old_output_moved = false;
    remove_quietly(backup);
    return {.success = true, .diagnostic = {}};
  } catch (const std::exception& error) {
    remove_quietly(staging);
    if (old_output_moved && !destination.empty() && !backup.empty() &&
        !std::filesystem::exists(destination)) {
      std::error_code restore_error;
      std::filesystem::rename(backup, destination, restore_error);
    }
    return {.success = false, .diagnostic = std::string{"事务式资产写出失败："} + error.what()};
  }
}

} // namespace gneiss::tooling
