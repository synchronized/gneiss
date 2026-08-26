// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "render/render_asset_loader.h"
#include "render/render_resource_service.h"

#include <gneiss/core/result.h>

#include <cstddef>
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
  auto memory = std::make_shared<memory_file_system>();
  memory->files.emplace(
      "models/triangle.mesh.json",
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[-0.5,-0.5,0],[0.5,-0.5,0],[0,0.5,0]]})");
  memory->files.emplace("materials/default.material.json",
                        R"({"format":"gneiss.material","version":1,"color":[0.25,0.5,0.75,1]})");
  memory->files.emplace("models/broken.mesh.json", "{");

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

  first_mesh = {};
  second_mesh = {};
  material = {};
  broken = {};
  loader.release_unused();
  if (cache.size() != 0U || resources.live_resource_count() != 0U) {
    return 6;
  }
  return 0;
} catch (...) {
  return 99;
}
