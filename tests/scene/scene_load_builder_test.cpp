// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/scene/scene_load_builder.hpp"
#include "engine/function/world/world_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include "runtime/loading/application_scene_state.hpp"
#include <algorithm>
#include <thread>

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
struct memory_files final : engine::resource::file_system {
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
  engine::resource::virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_internal::render_resource_service resources;
  engine::resource::asset_manager active_assets(vfs);
  engine::resource::resource_cache active_cache, candidate_cache;
  render_internal::render_asset_loader active_render(vfs, active_cache, resources);
  render_internal::render_asset_loader candidate_render(vfs, candidate_cache, resources);
  engine::resource::asset_manager candidate_assets(vfs);
  {
    render_internal::mesh_asset_lease first, second;
    render_internal::asset_diagnostic diagnostic;
    check(active_render.acquire_mesh("asset://m.mesh", first, diagnostic) == GNEISS_SUCCESS);
    check(active_render.acquire_mesh("asset://m.mesh", second, diagnostic) == GNEISS_SUCCESS);
    check(first.get() == second.get());
    active_assets.collect_unused();
    active_render.release_unused();
    check(resources.get_mesh(first.get()) != nullptr);
    const auto render_revision = active_render.revision();
    scene_internal::prefab_asset_lease prefab;
    scene_internal::scene_diagnostic prefab_diagnostic;
    check(active_assets.acquire("asset://p.prefab", prefab, prefab_diagnostic) == GNEISS_SUCCESS);
    check(prefab.get() != nullptr);
    check(active_render.revision() == render_revision);
    active_assets.collect_unused();
    check(resources.get_mesh(first.get()) != nullptr);
    std::unique_ptr<engine::resource::resource_cache::prepared_adoption> prepared;
    check(active_render.prepare_adoption({}, {}, {}, prepared) == GNEISS_ERROR_INVALID_ARGUMENT);
    check(!prepared);
    const std::array<engine::resource::asset_version_key, 1> versions{};
    check(active_render.prepare_adoption({}, versions, {}, prepared) ==
          GNEISS_ERROR_INVALID_ARGUMENT);
    const std::array<render_internal::render_asset_lease, 1> invalid_leases{};
    check(active_render.prepare_adoption(invalid_leases, versions, {}, prepared) ==
          GNEISS_ERROR_INVALID_ARGUMENT);
    render_internal::render_resource_service foreign_resources;
    engine::resource::resource_cache foreign_cache;
    render_internal::render_asset_loader foreign_assets(vfs, foreign_cache, foreign_resources);
    render_internal::mesh_asset_lease foreign_mesh;
    check(foreign_assets.acquire_mesh("asset://m.mesh", foreign_mesh, diagnostic) ==
          GNEISS_SUCCESS);
    const std::array foreign_leases{
        render_internal::render_asset_loader::asset_lease(foreign_mesh)};
    check(active_render.prepare_adoption(foreign_leases, versions, {}, prepared) ==
          GNEISS_ERROR_INVALID_ARGUMENT);
    check(!prepared);
  }
  auto active = std::make_unique<runtime::application_scene_state>();
  check(active->native_world() == nullptr && active->world() == GNEISS_NULL_WORLD &&
        active->scenes() == nullptr);
  check(active->initialize(active_assets, active_render) == GNEISS_SUCCESS);
  const auto initialized_world = active->world();
  auto* initialized_scenes = active->scenes();
  check(active->initialize(active_assets, active_render) == GNEISS_ERROR_INVALID_STATE);
  check(active->world() == initialized_world && active->scenes() == initialized_scenes);

  gneiss_scene_instance old_scene{};
  check(active->scenes()->load("asset://s.scene", &old_scene) == GNEISS_SUCCESS);
  scene_internal::scene_node_view old_info;
  check(active->scenes()->get_node_info(old_scene, 0U, old_info) == GNEISS_SUCCESS);
  render_internal::material_asset_lease old_material;
  render_internal::asset_diagnostic diagnostic;
  check(active_render.acquire_material("asset://m.material", old_material, diagnostic) ==
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
  auto candidate = std::make_unique<runtime::application_scene_state>();
  check(candidate->initialize(candidate_assets, candidate_render) == GNEISS_SUCCESS);
  std::vector<render_internal::render_asset_lease> captured{render_internal::render_asset_lease{}};
  check(candidate->scenes()->capture_render_assets(captured) == GNEISS_SUCCESS && captured.empty());
  check(!render_internal::render_asset_loader::asset_lease(render_internal::mesh_asset_lease{}));
  check(
      !render_internal::render_asset_loader::asset_lease(render_internal::material_asset_lease{}));
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
    check(candidate_render.stage_asset(
              std::move(asset), staged, value,
              render_internal::render_asset_loader::publication_mode::fixed_version) ==
          GNEISS_SUCCESS);
    staged.push_back(std::move(value));
  }
  check(candidate_render.publish_assets(staged) == GNEISS_SUCCESS);
  scene_internal::scene_load_builder::asset_bindings bindings;
  for (const auto& value : staged) {
    bindings.emplace(value.source.uri, value.lease);
  }
  auto material =
      render_internal::render_asset_loader::material_lease(bindings.at("asset://m.material"));
  auto previous_version = bindings.at("asset://m.material");
  const auto mesh_rid = bindings.at("asset://m.mesh").get();
  check(material && material.get() != old_material.get());
  check(resources.get_material(material.get())->green == 1.0F);
  check(!render_internal::render_asset_loader::mesh_lease(bindings.at("asset://m.material")));
  check(!render_internal::render_asset_loader::material_lease(bindings.at("asset://m.mesh")));
  render_internal::render_asset_lease uncached;
  check(candidate_render.acquire_cached(requests.front(), uncached) == GNEISS_ERROR_NOT_FOUND);
  // 在绑定选定后改变同 URI 的旧缓存；构建必须继续使用选定的绿色版本。
  files->files["m.material"] = R"({"format":"gneiss.material","version":1,"color":[0,0,1,1]})";
  render_internal::material_asset_lease unrelated;
  check(candidate_render.acquire_material("asset://m.material", unrelated, diagnostic) ==
        GNEISS_SUCCESS);
  check(unrelated.get() != material.get());
  staged.clear();
  const auto reads = files->reads;
  gneiss_scene_instance new_scene{};
  {
    scene_internal::scene_load_builder builder(*candidate->scenes(), std::move(prepared),
                                               std::move(bindings));
    bool complete = false;
    unsigned steps = 0;
    while (!complete && steps++ < 20U) {
      check(builder.advance(complete, 1U) == GNEISS_SUCCESS);
      std::uint64_t count{};
      check(gneiss_world_entity_count(active->world(), &count) == GNEISS_SUCCESS && count == 1U);
      check(resources.get_material(old_material.get())->red == 1.0F);
      check(files->reads == reads);
    }
    check(complete && steps > 3U && builder.completed_nodes() == 3U);
    new_scene = builder.instance();
  }
  // 候选仅持有一份运行时；移动、失败和取消不得泄漏或提前发布。
  {
    scene_internal::prefab_asset_lease source;
    scene_internal::prefab_instance_description author;
    check(candidate->scenes()->capture_prefab_header(
              new_scene, "00000000-0000-4000-8000-000000000004", author, source) == GNEISS_SUCCESS);
    const auto original_uuid = author.instance_uuid;
    const auto* original_source = source.get();
    check(candidate->scenes()->capture_prefab_header(new_scene, "missing", author, source) ==
          GNEISS_ERROR_NOT_FOUND);
    check(author.instance_uuid == original_uuid && source.get() == original_source);
    bool removal_complete = true;
    check(candidate->scenes()->prepare_prefab_removal(new_scene, "missing", removal_complete) ==
              GNEISS_ERROR_NOT_FOUND &&
          !removal_complete);
    author.instance_uuid = "00000000-0000-4000-8000-000000000077";
    author.prefab_uri = "asset://p.prefab";
    author.rotation = {0, 0, 0, 1};
    author.scale = {1, 1, 1};
    scene_internal::prefab_candidate pending;
    const auto transaction_reads = files->reads;
    check(candidate->scenes()->begin_prefab_candidate(new_scene, author, source, *source.get(),
                                                      pending) == GNEISS_SUCCESS);
    const auto hidden_root = pending.root();
    check(candidate->scenes()->begin_prefab_candidate(new_scene, author, source, *source.get(),
                                                      pending) == GNEISS_ERROR_INVALID_STATE);
    check(pending.root() == hidden_root);
    check(candidate->scenes()->commit_prefab_removal(new_scene, original_uuid, pending) ==
          GNEISS_ERROR_INVALID_STATE);
    check(pending.root() == hidden_root);
    scene_internal::prefab_candidate moved{std::move(pending)};
    check(!pending.is_valid() && moved.is_valid());
    check(candidate->scenes()->commit_prefab_candidate(old_scene, moved) ==
          GNEISS_ERROR_INVALID_HANDLE);
    check(candidate->scenes()->commit_prefab_candidate(new_scene, moved) ==
          GNEISS_ERROR_INVALID_STATE);
    gneiss_result foreign{};
    bool cleaned_on_worker = true;
    gneiss_result foreign_commit{};
    std::thread worker([&] {
      scene_internal::prefab_node_view preview;
      foreign = moved.preview(0, preview);
      foreign_commit = candidate->scenes()->commit_prefab_candidate(new_scene, moved);
      cleaned_on_worker = moved.cleanup_step(1);
    });
    worker.join();
    check(foreign == GNEISS_ERROR_INVALID_STATE && !cleaned_on_worker &&
          foreign_commit == GNEISS_ERROR_INVALID_STATE);
    scene_internal::prefab_candidate assigned;
    assigned = std::move(moved);
    check(!moved.is_valid() && assigned.is_valid());
    for (unsigned step = 0; step < 8 && assigned.is_valid(); ++step)
      (void)assigned.cleanup_step(1);
    check(!assigned.is_valid() && assigned.cleanup_step(1));
    engine::transform hidden;
    check(candidate->native_world()->node_get_local_transform(scene_node_id{hidden_root}, hidden) ==
          GNEISS_ERROR_INVALID_HANDLE);
    std::uint64_t count{};
    check(candidate->native_world()->entity_count(count) == GNEISS_SUCCESS && count == 3);
    check(files->reads == transaction_reads);
  }
  const auto selected_material = material.get();
  check(candidate->scenes()->capture_render_assets(captured) == GNEISS_SUCCESS);
  check(captured.size() == 2U && files->reads == reads);
  const auto has_rid = [&](std::uint64_t rid) {
    return std::ranges::any_of(captured, [rid](const auto& lease) { return lease.get() == rid; });
  };
  check(has_rid(mesh_rid) && has_rid(selected_material) && !has_rid(unrelated.get()));
  // 同 URI 的蓝色和绿色版本同时被场景使用时，不得按 URI 合并。
  gneiss_scene_instance additional{};
  check(candidate->scenes()->load("asset://s.scene", &additional) == GNEISS_SUCCESS);
  check(candidate->scenes()->capture_render_assets(captured) == GNEISS_SUCCESS);
  check(captured.size() == 4U && has_rid(selected_material) && has_rid(unrelated.get()));
  gneiss_scene_instance duplicate_scene{};
  check(candidate->scenes()->load("asset://s.scene", &duplicate_scene) == GNEISS_SUCCESS);
  check(candidate->scenes()->capture_render_assets(captured) == GNEISS_SUCCESS);
  check(captured.size() == 4U && has_rid(selected_material) && has_rid(unrelated.get()));
  check(candidate->scenes()->unload(duplicate_scene) == GNEISS_SUCCESS);
  check(candidate->scenes()->unload(additional) == GNEISS_SUCCESS);
  check(candidate->scenes()->capture_render_assets(captured) == GNEISS_SUCCESS);
  check(captured.size() == 2U && !has_rid(unrelated.get()));
  gneiss_result snapshot_thread = GNEISS_SUCCESS;
  std::thread snapshot_worker(
      [&] { snapshot_thread = candidate->scenes()->capture_render_assets(captured); });
  snapshot_worker.join();
  check(snapshot_thread == GNEISS_ERROR_INVALID_STATE && captured.size() == 2U &&
        has_rid(selected_material));
  material = {};
  candidate_render.release_unused();
  check(resources.get_material(selected_material) != nullptr &&
        resources.get_material(selected_material)->green == 1.0F);
  render_internal::render_asset_lease next_version;
  check(candidate_render.acquire_cached(requests.back(), next_version) == GNEISS_SUCCESS);
  std::array updates{scene_internal::scene_instance_service::asset_replacement{
      .previous = previous_version, .replacement = next_version}};
  gneiss_result other_thread = GNEISS_SUCCESS;
  std::thread worker([&] { other_thread = candidate->scenes()->replace_render_assets(updates); });
  worker.join();
  check(other_thread == GNEISS_ERROR_INVALID_STATE);
  auto duplicate = std::array{updates.front(), updates.front()};
  check(candidate->scenes()->replace_render_assets(duplicate) == GNEISS_ERROR_INVALID_ARGUMENT);
  {
    render_internal::render_resource_service foreign_resources;
    engine::resource::resource_cache foreign_cache;
    render_internal::render_asset_loader foreign_loader(vfs, foreign_cache, foreign_resources);
    render_internal::material_asset_lease foreign_material;
    check(foreign_loader.acquire_material("asset://m.material", foreign_material, diagnostic) ==
          GNEISS_SUCCESS);
    render_internal::render_asset_lease foreign_lease;
    check(foreign_loader.acquire_cached(requests.back(), foreign_lease) == GNEISS_SUCCESS);
    const std::array invalid{scene_internal::scene_instance_service::asset_replacement{
        .previous = previous_version, .replacement = foreign_lease}};
    check(candidate->scenes()->replace_render_assets(invalid) == GNEISS_ERROR_INVALID_ARGUMENT);
  }
  scene_internal::scene_node_view ordinary;
  scene_internal::prefab_node_view prefab_node;
  check(candidate->scenes()->get_node_info(new_scene, 0U, ordinary) == GNEISS_SUCCESS);
  check(candidate->scenes()->get_prefab_node_info(new_scene, 1U, prefab_node) == GNEISS_SUCCESS);
  const gneiss::world_internal::mesh_renderer expected{.mesh = mesh_rid,
                                                       .material = selected_material};
  const std::array unchanged{world_internal::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = expected, .replacement = expected}};
  const auto edit_reads = files->reads;
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://m.material") == GNEISS_SUCCESS);
  check(files->reads == edit_reads);
  check(world_internal::replace_mesh_renderers(candidate->world(), unchanged) == GNEISS_SUCCESS);
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://missing.material") ==
        GNEISS_ERROR_NOT_FOUND);
  check(world_internal::replace_mesh_renderers(candidate->world(), unchanged) == GNEISS_SUCCESS);
  // 第二个使用者已被外部改动时，第一个也不能先切换。
  check(world_internal::entity_set_mesh_renderer(candidate->world(), prefab_node.entity.get(),
                                                 {.mesh = mesh_rid, .material = unrelated.get()}) ==
        GNEISS_SUCCESS);
  check(candidate->scenes()->replace_render_assets(updates) == GNEISS_ERROR_INVALID_STATE);
  check(world_internal::replace_mesh_renderers(candidate->world(), unchanged) == GNEISS_SUCCESS);
  check(world_internal::entity_set_mesh_renderer(candidate->world(), prefab_node.entity.get(),
                                                 expected) == GNEISS_SUCCESS);
  check(candidate->scenes()->replace_render_assets(updates) == GNEISS_SUCCESS);
  const gneiss::world_internal::mesh_renderer updated{.mesh = mesh_rid,
                                                      .material = next_version.get()};
  const std::array committed{
      world_internal::mesh_renderer_replacement{
          .entity = ordinary.entity, .expected = updated, .replacement = updated},
      world_internal::mesh_renderer_replacement{
          .entity = prefab_node.entity, .expected = updated, .replacement = updated}};
  check(world_internal::replace_mesh_renderers(candidate->world(), committed) == GNEISS_SUCCESS);
  files->files["other.material"] = files->files["m.material"];
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://other.material") == GNEISS_SUCCESS);
  render_internal::material_asset_lease edited_material;
  check(candidate_render.acquire_material("asset://other.material", edited_material, diagnostic) ==
        GNEISS_SUCCESS);
  const gneiss::world_internal::mesh_renderer edited{.mesh = mesh_rid,
                                                     .material = edited_material.get()};
  const std::array material_only{world_internal::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = edited, .replacement = edited}};
  check(world_internal::replace_mesh_renderers(candidate->world(), material_only) ==
        GNEISS_SUCCESS);
  check(resources.get_material(old_material.get())->red == 1.0F);
  duplicate = {};
  updates = {};
  previous_version = {};
  candidate_render.release_unused();
  check(resources.get_material(selected_material) != nullptr);
  captured.clear();
  candidate_render.release_unused();
  check(resources.get_material(selected_material) == nullptr);
  {
    using binding = scene_internal::scene_instance_service::mesh_binding;
    binding before;
    const auto reads = files->reads;
    check(candidate->scenes()->capture_mesh_binding(new_scene, ordinary.node, before) ==
          GNEISS_SUCCESS);
    binding after{.mesh = before.mesh, .material = next_version};
    auto invalid = after;
    invalid.material = after.mesh;
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, invalid) ==
          GNEISS_ERROR_INVALID_ARGUMENT);
    gneiss_result worker_result{};
    std::thread edit_worker([&] {
      worker_result =
          candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after);
    });
    edit_worker.join();
    check(worker_result == GNEISS_ERROR_INVALID_STATE);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after) ==
          GNEISS_SUCCESS);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after) ==
          GNEISS_ERROR_INVALID_STATE);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, after, before) ==
          GNEISS_SUCCESS);
    const scene_internal::mesh_renderer_node_creation creation{
        .uuid = "00000000-0000-4000-8000-000000000099",
        .name = "prepared",
        .parent = {},
        .mesh_uri = before.mesh.uri(),
        .material_uri = before.material.uri(),
    };
    gneiss::scene_node_id created{};
    check(candidate->scenes()->create_mesh_renderer_node_ready(
              new_scene, creation, invalid, created) == GNEISS_ERROR_INVALID_ARGUMENT &&
          !created.is_valid());
    check(candidate->scenes()->create_mesh_renderer_node_ready(new_scene, creation, before,
                                                               created) == GNEISS_SUCCESS);
    const auto created_node = created;
    check(candidate->scenes()->create_mesh_renderer_node_ready(
              new_scene, creation, before, created) == GNEISS_ERROR_INVALID_ARGUMENT &&
          !created.is_valid());
    check(candidate->scenes()->destroy_node(new_scene, gneiss::scene_node_id{created_node}) ==
          GNEISS_SUCCESS);
    check(files->reads == reads);
  }
  check(candidate->scenes()->remove_mesh_renderer(new_scene, ordinary.node) == GNEISS_SUCCESS);
  const auto snapshot_reads = files->reads;
  check(candidate->scenes()->capture_render_assets(captured) == GNEISS_SUCCESS);
  check(captured.size() == 2U && has_rid(mesh_rid) && has_rid(next_version.get()));
  check(files->reads == snapshot_reads);
  captured.clear();
  const auto old_world = active->world();
  active.swap(candidate);
  std::uint64_t count{};
  check(gneiss_world_entity_count(active->world(), &count) == GNEISS_SUCCESS && count == 3U);
  check(active->scenes()->get_node_count(old_scene, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(active->scenes()->get_node_count(new_scene, &count) == GNEISS_SUCCESS && count == 1U);
  old_material = {};
  candidate.reset();
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(old_pixels->red == 1.0F);

  // 构造失败时仅回收候选节点；即使依赖缺失也不能退回主线程文件读取。
  check(scene_internal::prepare_scene_description(vfs, "asset://s.scene", prepared,
                                                  scene_diagnostic, {}) == GNEISS_SUCCESS);
  candidate = std::make_unique<runtime::application_scene_state>();
  check(candidate->initialize(candidate_assets, candidate_render) == GNEISS_SUCCESS);
  const auto failure_reads = files->reads;
  {
    scene_internal::scene_load_builder builder(*candidate->scenes(), std::move(prepared));
    bool complete{};
    check(builder.advance(complete) == GNEISS_ERROR_NOT_READY && !complete);
  }
  check(files->reads == failure_reads);
  check(gneiss_world_entity_count(candidate->world(), &count) == GNEISS_SUCCESS && count == 0U);
  check(gneiss_world_entity_count(active->world(), &count) == GNEISS_SUCCESS && count == 3U);
}
} // namespace

int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
