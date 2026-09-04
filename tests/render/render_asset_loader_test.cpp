// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/mesh_binary.h"
#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "render/render_asset_loader.h"
#include "render/render_resource_service.h"

#include <gneiss/core/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

class memory_file_system final : public gneiss::asset_internal::file_system {
public:
  std::unordered_map<std::string, std::string> files;
  mutable std::size_t read_count = 0;

  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override {
    ++read_count;
    try {
      const auto found = files.find(std::string(path));
      if (found == files.end()) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      out_bytes.resize(found->second.size());
      for (std::size_t index = 0; index < found->second.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(found->second[index]);
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};

void add_indexed_mesh(memory_file_system& memory) {
  gneiss::asset_internal::mesh_binary_data data{.vertices = {{.position = {-0.5F, -0.5F, 0.0F},
                                                              .texcoord = {0.0F, 0.0F},
                                                              .normal = {0.0F, 0.0F, 1.0F}},
                                                             {.position = {0.5F, -0.5F, 0.0F},
                                                              .texcoord = {1.0F, 0.0F},
                                                              .normal = {0.0F, 0.0F, 1.0F}},
                                                             {.position = {0.5F, 0.5F, 0.0F},
                                                              .texcoord = {1.0F, 1.0F},
                                                              .normal = {0.0F, 0.0F, 1.0F}},
                                                             {.position = {-0.5F, 0.5F, 0.0F},
                                                              .texcoord = {0.0F, 1.0F},
                                                              .normal = {0.0F, 0.0F, 1.0F}}},
                                                .indices = {0U, 1U, 2U, 0U, 2U, 3U}};
  std::vector<std::byte> bytes;
  gneiss::asset_internal::mesh_binary_diagnostic diagnostic;
  if (gneiss::asset_internal::encode_mesh_binary(data, bytes, diagnostic) !=
      gneiss::asset_internal::mesh_binary_result::success) {
    throw std::runtime_error{"无法创建索引 Mesh 测试数据"};
  }
  memory.files.emplace("models/indexed.gneiss-mesh",
                       std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

[[nodiscard]] bool
indexed_mesh_is_preserved(gneiss::render_internal::render_asset_loader& loader,
                          const gneiss::render_internal::render_resource_service& resources) {
  gneiss::render_internal::mesh_asset_lease mesh;
  gneiss::render_internal::asset_diagnostic diagnostic;
  if (loader.acquire_mesh("asset://models/indexed.gneiss-mesh", mesh, diagnostic) !=
      GNEISS_SUCCESS) {
    return false;
  }
  const auto* resource = resources.get_mesh(mesh.get());
  return resource != nullptr && resource->vertices.size() == 4U && resource->normals.size() == 4U &&
         resource->indices.size() == 6U && resource->indices[5] == 3U;
}

} // namespace

int main() try { // NOLINT(readability-function-cognitive-complexity)：集成测试按返回码定位阶段。
  static constexpr std::array<std::uint8_t, 68> png_bytes = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00,
      0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00, 0x03, 0x03, 0x02, 0x00, 0xef, 0xbf, 0x6b, 0x99,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  auto memory = std::make_shared<memory_file_system>();
  memory->files.emplace(
      "models/triangle.mesh.json",
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]]})");
  memory->files.emplace("materials/default.material.json",
                        R"({"format":"gneiss.material","version":1,"color":[0.25,0.5,0.75,1]})");
  memory->files.emplace("models/broken.mesh.json", "{");
  const std::string png_data(reinterpret_cast<const char*>(png_bytes.data()), png_bytes.size());
  memory->files.emplace("textures/white.png", png_data);
  memory->files.emplace(
      "textures/white.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://textures/white.png","color_space":"srgb"})");
  memory->files.emplace(
      "textures/missing.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://textures/missing.png","color_space":"linear"})");
  memory->files.emplace(
      "models/textured.mesh.json",
      R"({"format":"gneiss.mesh","version":2,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]],"uvs":[[0,0],[1,0],[0.5,1]]})");
  memory->files.emplace(
      "models/lit.mesh.json",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})");
  memory->files.emplace(
      "models/invalid-normal.mesh.json",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,2],[0,0,1],[0,0,1]]})");
  memory->files.emplace(
      "materials/textured.material.json",
      R"({"format":"gneiss.material","version":2,"color":[1,0.5,0.25,1],"base_color_texture":"asset://textures/white.texture.json"})");
  add_indexed_mesh(*memory);

  gneiss::asset_internal::virtual_file_system file_system;
  gneiss::asset_internal::resource_cache cache;
  gneiss::render_internal::render_resource_service resources;
  gneiss::render_internal::render_asset_loader loader(file_system, cache, resources);
  if (file_system.mount("asset://", memory) != GNEISS_SUCCESS) {
    return 1;
  }

  gneiss::render_internal::mesh_asset_lease first_mesh;
  gneiss::render_internal::mesh_asset_lease second_mesh;
  gneiss::render_internal::material_asset_lease material;
  gneiss::render_internal::asset_diagnostic diagnostic;
  if (loader.acquire_mesh("asset://models/triangle.mesh.json", first_mesh, diagnostic) !=
          GNEISS_SUCCESS ||
      loader.acquire_mesh("asset://models/triangle.mesh.json", second_mesh, diagnostic) !=
          GNEISS_SUCCESS ||
      first_mesh.get() == GNEISS_NULL_MESH || first_mesh.get() != second_mesh.get() ||
      memory->read_count != 1U ||
      loader.acquire_material("asset://materials/default.material.json", material, diagnostic) !=
          GNEISS_SUCCESS ||
      material.get() == GNEISS_NULL_MATERIAL || resources.live_resource_count() != 2U) {
    return 2;
  }

  gneiss::render_internal::material_asset_lease wrong_type;
  if (loader.acquire_material("asset://models/triangle.mesh.json", wrong_type, diagnostic) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      wrong_type) {
    return 3;
  }

  gneiss::render_internal::mesh_asset_lease broken;
  if (loader.acquire_mesh("asset://models/broken.mesh.json", broken, diagnostic) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      diagnostic.byte_offset == 0U || broken) {
    return 4;
  }
  memory->files["models/broken.mesh.json"] =
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  if (loader.acquire_mesh("asset://models/broken.mesh.json", broken, diagnostic) !=
          GNEISS_SUCCESS ||
      broken.get() == GNEISS_NULL_MESH || resources.live_resource_count() != 3U) {
    return 5;
  }

  gneiss::render_internal::texture_asset_lease first_texture;
  gneiss::render_internal::texture_asset_lease second_texture;
  if (loader.acquire_texture("asset://textures/white.texture.json", first_texture, diagnostic) !=
          GNEISS_SUCCESS ||
      loader.acquire_texture("asset://textures/white.texture.json", second_texture, diagnostic) !=
          GNEISS_SUCCESS ||
      first_texture.get() == GNEISS_NULL_TEXTURE || first_texture.get() != second_texture.get()) {
    return 6;
  }
  const auto* texture = resources.get_texture(first_texture.get());
  if (texture == nullptr || texture->width != 1U || texture->height != 1U ||
      texture->color_space != GNEISS_TEXTURE_COLOR_SPACE_SRGB || texture->pixels.size() != 4U ||
      resources.live_resource_count() != 4U) {
    return 7;
  }

  gneiss::render_internal::texture_asset_lease missing_texture;
  if (loader.acquire_texture("asset://textures/missing.texture.json", missing_texture,
                             diagnostic) != GNEISS_ERROR_NOT_FOUND ||
      missing_texture || diagnostic.path != "/source") {
    return 8;
  }
  memory->files.emplace("textures/missing.png", png_data);
  if (loader.acquire_texture("asset://textures/missing.texture.json", missing_texture,
                             diagnostic) != GNEISS_SUCCESS ||
      missing_texture.get() == GNEISS_NULL_TEXTURE || resources.live_resource_count() != 5U) {
    return 9;
  }

  gneiss::render_internal::mesh_asset_lease textured_mesh;
  gneiss::render_internal::material_asset_lease textured_material;
  if (loader.acquire_mesh("asset://models/textured.mesh.json", textured_mesh, diagnostic) !=
          GNEISS_SUCCESS ||
      loader.acquire_material("asset://materials/textured.material.json", textured_material,
                              diagnostic) != GNEISS_SUCCESS) {
    return 10;
  }
  const auto* mesh_resource = resources.get_mesh(textured_mesh.get());
  const auto* material_resource = resources.get_material(textured_material.get());
  if (mesh_resource == nullptr || mesh_resource->vertices.size() != 3U ||
      !mesh_resource->normals.empty() || mesh_resource->vertices[1].u != 1.0F ||
      mesh_resource->vertices[2].v != 1.0F || material_resource == nullptr ||
      material_resource->base_color_texture != first_texture.get() ||
      resources.live_resource_count() != 7U) {
    return 11;
  }

  gneiss::render_internal::mesh_asset_lease lit_mesh;
  gneiss::render_internal::mesh_asset_lease invalid_normal_mesh;
  if (loader.acquire_mesh("asset://models/lit.mesh.json", lit_mesh, diagnostic) != GNEISS_SUCCESS) {
    return 13;
  }
  const auto* lit_resource = resources.get_mesh(lit_mesh.get());
  if (lit_resource == nullptr || lit_resource->normals.size() != 3U ||
      lit_resource->normals[0].z != 1.0F ||
      loader.acquire_mesh("asset://models/invalid-normal.mesh.json", invalid_normal_mesh,
                          diagnostic) != GNEISS_ERROR_INVALID_ARGUMENT ||
      diagnostic.path != "/normals/0") {
    return 14;
  }
  if (!indexed_mesh_is_preserved(loader, resources)) {
    return 15;
  }

  const auto old_mesh = textured_mesh.get();
  const auto old_material = textured_material.get();
  const auto old_texture = first_texture.get();
  memory->files["models/textured.mesh.json"] =
      R"({"format":"gneiss.mesh","version":2,"topology":"triangle_list","vertices":[[-1,-1,0],[1,-1,0],[0,1,0]],"uvs":[[0,0],[1,0],[0.5,1]]})";
  memory->files["materials/textured.material.json"] =
      R"({"format":"gneiss.material","version":2,"color":[0.2,0.4,0.6,1],"base_color_texture":"asset://textures/white.texture.json"})";
  const std::array reloads{gneiss::render_internal::render_asset_reload{
                               .uri = "asset://models/textured.mesh.json",
                               .type = gneiss::render_internal::render_asset_type::mesh},
                           gneiss::render_internal::render_asset_reload{
                               .uri = "asset://materials/textured.material.json",
                               .type = gneiss::render_internal::render_asset_type::material},
                           gneiss::render_internal::render_asset_reload{
                               .uri = "asset://textures/white.texture.json",
                               .type = gneiss::render_internal::render_asset_type::texture}};
  if (loader.reload_assets(reloads, diagnostic) != GNEISS_SUCCESS) {
    return 16;
  }
  gneiss::render_internal::mesh_asset_lease reloaded_mesh;
  gneiss::render_internal::material_asset_lease reloaded_material;
  gneiss::render_internal::texture_asset_lease reloaded_texture;
  if (loader.acquire_mesh(reloads[0].uri, reloaded_mesh, diagnostic) != GNEISS_SUCCESS ||
      loader.acquire_material(reloads[1].uri, reloaded_material, diagnostic) != GNEISS_SUCCESS ||
      loader.acquire_texture(reloads[2].uri, reloaded_texture, diagnostic) != GNEISS_SUCCESS ||
      reloaded_mesh.get() == old_mesh || reloaded_material.get() == old_material ||
      reloaded_texture.get() == old_texture) {
    return 17;
  }
  const auto* reloaded_material_resource = resources.get_material(reloaded_material.get());
  if (reloaded_material_resource == nullptr ||
      reloaded_material_resource->base_color_texture != reloaded_texture.get() ||
      resources.get_mesh(old_mesh) == nullptr || resources.get_material(old_material) == nullptr ||
      resources.get_texture(old_texture) == nullptr) {
    return 18;
  }
  memory->files["materials/textured.material.json"] = "{";
  if (loader.reload_assets(reloads, diagnostic) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 19;
  }
  gneiss::render_internal::material_asset_lease after_failed_reload;
  if (loader.acquire_material(reloads[1].uri, after_failed_reload, diagnostic) != GNEISS_SUCCESS ||
      after_failed_reload.get() != reloaded_material.get()) {
    return 20;
  }

  first_mesh = {};
  second_mesh = {};
  material = {};
  broken = {};
  first_texture = {};
  second_texture = {};
  missing_texture = {};
  textured_mesh = {};
  lit_mesh = {};
  reloaded_mesh = {};
  reloaded_material = {};
  reloaded_texture = {};
  after_failed_reload = {};
  loader.release_unused();
  if (cache.size() != 0U) {
    return 21;
  }
  if (resources.live_resource_count() != 2U) {
    return 22;
  }
  if (resources.get_texture(old_texture) == nullptr) {
    return 23;
  }
  textured_material = {};
  loader.release_unused();
  if (cache.size() != 0U || resources.live_resource_count() != 0U) {
    return 13;
  }
  return 0;
} catch (...) {
  return 99;
}
