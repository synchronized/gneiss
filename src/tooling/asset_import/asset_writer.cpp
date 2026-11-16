// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.hpp"
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <gneiss/engine/resource/scene/scene_description.hpp>
#include <gneiss/engine/resource/texture/texture_decoder.hpp>

#include <gneiss/engine/resource/mesh/mesh_binary.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace gneiss::tooling {
namespace {

[[nodiscard]] std::string uuid_for(std::size_t index, bool component = false) {
  std::ostringstream stream;
  stream << (component ? "10000000-0000-4000-8000-" : "00000000-0000-4000-8000-") << std::hex
         << std::setfill('0') << std::setw(12) << index + 1U;
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
  gneiss::mesh_binary_data data;
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
  auto bytes = gneiss::encode_mesh_binary(data);
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

template <class Bytes> bool write_bytes(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return stream.good();
}

[[nodiscard]] bool write_material(const import_ir_material& material,
                                  const std::filesystem::path& path,
                                  std::string_view asset_uri_prefix) {
  gneiss::material_description description;
  auto& p = description.parameters;
  p.color = {material.base_color[0], material.base_color[1], material.base_color[2],
             material.base_color[3]};
  p.metallic = material.metallic;
  p.roughness = material.roughness;
  p.normal_scale = material.normal_scale;
  p.occlusion_strength = material.occlusion_strength;
  p.emissive = {material.emissive[0], material.emissive[1], material.emissive[2]};
  p.alpha_mode = material.alpha_mode;
  p.double_sided = material.double_sided != 0;
  p.alpha_cutoff = material.alpha_cutoff;
  p.sampling = material.sampling;
  constexpr std::array variants{0U, 1U, 2U, 1U, 0U};
  const auto images = material.texture_indices();
  for (std::size_t slot = 0; slot < images.size(); ++slot)
    if (images[slot])
      description.texture_uris[slot] = std::string(asset_uri_prefix) + "textures/image-" +
                                       std::to_string(*images[slot]) +
                                       image_variant_suffixes[variants[slot]] + ".texture.json";
  auto bytes = gneiss::asset_file::serialize_bytes(description);
  return bytes && write_bytes(path, *bytes);
}

[[nodiscard]] bool write_scene(const import_ir& data, const std::filesystem::path& path,
                               std::string_view asset_uri_prefix) {
  gneiss::scene_description scene;
  scene.uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000000");
  scene.objects.resize(data.nodes.size());
  auto renderer = [&](std::size_t mesh, std::size_t primitive) {
    return gneiss::mesh_component_description{
        std::string(asset_uri_prefix) + "models/" + mesh_name(mesh, primitive),
        std::string(asset_uri_prefix) + "materials/" +
            material_name(data.meshes[mesh].primitives[primitive])};
  };
  for (std::size_t i = 0; i < data.nodes.size(); ++i) {
    const auto& node = data.nodes[i];
    auto& object = scene.objects[i];
    object.uuid = *gneiss::guid::parse(uuid_for(i));
    object.name = node.name;
    object.transform = {node.translation, node.rotation, node.scale};
    for (auto child : node.children)
      scene.objects[child].parent.object = object.uuid;
    // 沿用导入对象的确定性索引规则，重复导入相同输入不改变组件身份。
    if (node.mesh_index && data.meshes[*node.mesh_index].primitives.size() == 1) {
      auto component = renderer(*node.mesh_index, 0);
      component.uuid = *gneiss::guid::parse(uuid_for(i, true));
      object.components.set(std::move(component));
    }
  }
  for (std::size_t i = 0; i < data.nodes.size(); ++i) {
    const auto& node = data.nodes[i];
    if (!node.mesh_index || data.meshes[*node.mesh_index].primitives.size() <= 1)
      continue;
    for (std::size_t j = 0; j < data.meshes[*node.mesh_index].primitives.size(); ++j) {
      gneiss::game_object_description child;
      child.uuid = *gneiss::guid::parse(uuid_for(scene.objects.size()));
      child.parent.object = scene.objects[i].uuid;
      auto component = renderer(*node.mesh_index, j);
      component.uuid = *gneiss::guid::parse(uuid_for(scene.objects.size(), true));
      child.components.set(std::move(component));
      scene.objects.push_back(std::move(child));
    }
  }
  auto bytes = gneiss::asset_file::serialize_bytes(scene);
  return bytes && write_bytes(path, *bytes);
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
        gneiss::texture_description description{
            std::string(asset_uri_prefix) + "textures/" + base + ".png",
            variant == 0U ? gneiss::texture_color_space::srgb : gneiss::texture_color_space::linear,
            variant == 2U ? gneiss::texture_usage::normal : gneiss::texture_usage::default_usage};
        auto texture = gneiss::detail::encode_texture(description);
        if (!image.good() || !texture ||
            !write_bytes(output_directory / "textures" / (base + ".texture.json"), *texture))
          return {.success = false, .diagnostic = "写出 Texture 失败"};
      }
    }
    if (!write_scene(data, output_directory / "scenes" / "scene.scene.json", asset_uri_prefix)) {
      return {.success = false, .diagnostic = "写出 Scene 失败"};
    }
    return {.success = true, .diagnostic = {}};
  } catch (const std::filesystem::filesystem_error& error) {
    return {.success = false, .diagnostic = std::string{"资产写出失败："} + error.what()};
  }
}

bool remove_quietly(const std::filesystem::path& path) noexcept {
  try {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    return !error;
  } catch (const std::filesystem::filesystem_error&) {
    // 清理是尽力而为；调用方的主错误和恢复流程优先。
    return false;
  }
}

} // namespace

write_report write_assets(const import_ir& data, const std::filesystem::path& output_directory,
                          std::string_view asset_uri_prefix) {
  if (gneiss::initialize_meta() != gneiss::error::success)
    return {.success = false, .diagnostic = "初始化资源类型失败"};
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
  } catch (const std::filesystem::filesystem_error& error) {
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
