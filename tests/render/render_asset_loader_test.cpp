// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "render/render_asset_loader.h"
#include "render/render_resource_service.h"

#include <gneiss/core/result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
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

} // namespace

int main() try {
  static constexpr std::uint8_t png_bytes[] = {
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
  const std::string png_data(reinterpret_cast<const char*>(png_bytes), sizeof(png_bytes));
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
      "materials/textured.material.json",
      R"({"format":"gneiss.material","version":2,"color":[1,0.5,0.25,1],"base_color_texture":"asset://textures/white.texture.json"})");

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
      mesh_resource->vertices[1].u != 1.0F || mesh_resource->vertices[2].v != 1.0F ||
      material_resource == nullptr ||
      material_resource->base_color_texture != first_texture.get() ||
      resources.live_resource_count() != 7U) {
    return 11;
  }

  first_mesh = {};
  second_mesh = {};
  material = {};
  broken = {};
  first_texture = {};
  second_texture = {};
  missing_texture = {};
  textured_mesh = {};
  loader.release_unused();
  if (cache.size() != 2U || resources.live_resource_count() != 2U ||
      resources.get_texture(material_resource->base_color_texture) == nullptr) {
    return 12;
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
