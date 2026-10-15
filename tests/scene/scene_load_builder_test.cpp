// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/application/application_scene_state.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/scene/scene_load_builder.hpp"

#include <cstdio>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("场景候选隔离失败，行=" + std::to_string(where.line()));
  }
}
struct memory_files final : asset_internal::file_system {
  std::map<std::string, std::string> files;
  mutable unsigned reads{};
  gneiss_result read(std::string_view path, std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& bytes) const noexcept override try {
    ++reads;
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (found->second.size() > limit) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
};
constexpr std::string_view object =
    R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}})";

void run() {
  auto files = std::make_shared<memory_files>();
  files->files["s.scene"] =
      R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)" +
      std::string(object) + R"(],"prefab_instances":[]})";
  files->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000003","objects":[)" +
      std::string(object) + "]}";
  files->files["m.mesh"] =
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  files->files["m.material"] = R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
  asset_internal::virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_internal::render_resource_service resources;
  auto active = std::make_unique<application_internal::application_scene_state>(vfs, resources);
  check(active->initialize() == GNEISS_SUCCESS);
  gneiss_scene_instance old_scene{};
  check(active->scenes->load("asset://s.scene", &old_scene) == GNEISS_SUCCESS);
  scene_internal::scene_node_view old_info;
  check(active->scenes->get_node_info(old_scene, 0U, old_info) == GNEISS_SUCCESS);
  render_internal::material_asset_lease old_material;
  render_internal::asset_diagnostic diagnostic;
  check(active->assets.acquire_material("asset://m.material", old_material, diagnostic) ==
        GNEISS_SUCCESS);
  auto old_pixels = resources.share_material(old_material.get());
  check(old_pixels && old_pixels->red == 1.0F);
  files->files["m.material"] = R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  const auto empty_instances = files->files["s.scene"].find("\"prefab_instances\":[]");
  files->files["s.scene"].replace(
      empty_instances, 21U,
      R"("prefab_instances":[{"instance_uuid":"00000000-0000-4000-8000-000000000004","prefab":"asset://p.prefab","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"overrides":[]}])");
  scene_internal::prepared_scene_description prepared;
  scene_internal::scene_diagnostic scene_diagnostic;
  check(scene_internal::prepare_scene_description(vfs, "asset://s.scene", prepared,
                                                  scene_diagnostic, {}) == GNEISS_SUCCESS);
  auto candidate = std::make_unique<application_internal::application_scene_state>(vfs, resources);
  check(candidate->initialize() == GNEISS_SUCCESS);
  const std::array requests{
      render_internal::render_asset_reload{"asset://m.mesh",
                                           render_internal::render_asset_type::mesh},
      render_internal::render_asset_reload{"asset://m.material",
                                           render_internal::render_asset_type::material}};
  render_internal::prepared_render_batch batch;
  check(render_internal::prepare_render_assets(vfs, requests, batch, diagnostic, {}) ==
        GNEISS_SUCCESS);
  std::vector<render_internal::render_asset_loader::asset_candidate> staged;
  for (auto& asset : batch.assets) {
    render_internal::render_asset_loader::asset_candidate value;
    check(candidate->assets.stage_asset(
              std::move(asset), staged, value,
              render_internal::render_asset_loader::publication_mode::fixed_version) ==
          GNEISS_SUCCESS);
    staged.push_back(std::move(value));
  }
  check(candidate->assets.publish_assets(staged) == GNEISS_SUCCESS);
  scene_internal::scene_load_builder::asset_bindings bindings;
  for (const auto& value : staged) {
    bindings.emplace(value.source.uri, value.lease);
  }
  auto material =
      render_internal::render_asset_loader::material_lease(bindings.at("asset://m.material"));
  check(material && material.get() != old_material.get());
  check(resources.get_material(material.get())->green == 1.0F);
  check(!render_internal::render_asset_loader::mesh_lease(bindings.at("asset://m.material")));
  check(!render_internal::render_asset_loader::material_lease(bindings.at("asset://m.mesh")));
  render_internal::render_asset_lease uncached;
  check(candidate->assets.acquire_cached(requests.front(), uncached) == GNEISS_ERROR_NOT_FOUND);
  // 在绑定选定后改变同 URI 的旧缓存；构建必须继续使用选定的绿色版本。
  files->files["m.material"] = R"({"format":"gneiss.material","version":1,"color":[0,0,1,1]})";
  render_internal::material_asset_lease unrelated;
  check(candidate->assets.acquire_material("asset://m.material", unrelated, diagnostic) ==
        GNEISS_SUCCESS);
  check(unrelated.get() != material.get());
  staged.clear();
  const auto reads = files->reads;
  gneiss_scene_instance new_scene{};
  {
    scene_internal::scene_load_builder builder(*candidate->scenes, std::move(prepared),
                                               std::move(bindings));
    bool complete = false;
    unsigned steps = 0;
    while (!complete && steps++ < 20U) {
      check(builder.advance(complete, 1U) == GNEISS_SUCCESS);
      std::uint64_t count{};
      check(gneiss_world_entity_count(active->world, &count) == GNEISS_SUCCESS && count == 1U);
      check(resources.get_material(old_material.get())->red == 1.0F);
      check(files->reads == reads);
    }
    check(complete && steps > 3U && builder.completed_nodes() == 3U);
    new_scene = builder.instance();
  }
  const auto selected_material = material.get();
  material = {};
  candidate->assets.release_unused();
  check(resources.get_material(selected_material) != nullptr &&
        resources.get_material(selected_material)->green == 1.0F);
  const auto old_world = active->world;
  active.swap(candidate);
  std::uint64_t count{};
  check(gneiss_world_entity_count(active->world, &count) == GNEISS_SUCCESS && count == 3U);
  check(active->scenes->get_node_count(old_scene, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(active->scenes->get_node_count(new_scene, &count) == GNEISS_SUCCESS && count == 1U);
  old_material = {};
  candidate.reset();
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(old_pixels->red == 1.0F);

  // 构造失败时仅回收候选节点；即使依赖缺失也不能退回主线程文件读取。
  check(scene_internal::prepare_scene_description(vfs, "asset://s.scene", prepared,
                                                  scene_diagnostic, {}) == GNEISS_SUCCESS);
  candidate = std::make_unique<application_internal::application_scene_state>(vfs, resources);
  check(candidate->initialize() == GNEISS_SUCCESS);
  const auto failure_reads = files->reads;
  {
    scene_internal::scene_load_builder builder(*candidate->scenes, std::move(prepared));
    bool complete{};
    check(builder.advance(complete) == GNEISS_ERROR_NOT_READY && !complete);
  }
  check(files->reads == failure_reads);
  check(gneiss_world_entity_count(candidate->world, &count) == GNEISS_SUCCESS && count == 0U);
  check(gneiss_world_entity_count(active->world, &count) == GNEISS_SUCCESS && count == 3U);
}
} // namespace

int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
