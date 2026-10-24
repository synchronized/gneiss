// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/function/world/world_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include "runtime/loading/scene_domain.hpp"
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
  gneiss::engine::core::result read(std::string_view path,
                                    std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss::engine::core::result read_bounded(std::string_view path, std::size_t limit,
                                            std::vector<std::byte>& bytes) const noexcept override
      try {
    ++reads;
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::core::result::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::core::result::invalid_argument;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    return gneiss::engine::core::result::success;
  } catch (...) {
    return gneiss::engine::core::result::out_of_memory;
  }
};
constexpr std::string_view object =
    R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":{"mesh_renderer":{"mesh":"asset://m.mesh","material":"asset://m.material"}}})";

void unified_resource_index() {
  using namespace engine::resource;
  using engine::core::result;
  class text_loader final : public asset_loader {
  public:
    std::type_index target_type() const noexcept override { return typeid(std::string); }
    std::size_t maximum_bytes() const noexcept override { return 64U; }
    result decode(const std::vector<std::byte>& bytes,
                  std::shared_ptr<const void>& output) const override {
      output = std::make_shared<const std::string>(reinterpret_cast<const char*>(bytes.data()),
                                                   bytes.size());
      return result::success;
    }
  };
  auto source = std::make_shared<memory_files>();
  source->files["same"] = "cpu";
  virtual_file_system files;
  check(files.mount("asset://", source) == result::success);
  engine::render::render_resource_service resources;
  asset_manager assets(files);
  engine::render::render_asset_loader render(files, assets.cache(), resources);
  check(assets.register_loader(std::make_shared<text_loader>()) == result::success);
  asset_ref<const std::string> cpu;
  check(assets.load_sync("asset://same", cpu) == result::success);
  prefab_description description;
  description.objects.emplace_back();
  prefab_asset_ref prefab;
  check(assets.prefab_loader().install_prepared("asset://same", std::move(description), prefab) ==
        result::success);
  engine::render::render_asset_ref material;
  check(render.reserve_asset({"asset://same", engine::render::render_asset_type::material},
                             material) == result::success);
  assets.collect_unused();
  check(assets.cache().size() == 3U && *cpu == "cpu" && resources.live_resource_count() == 1U);
  prefab.reset();
  assets.collect_unused();
  check(assets.cache().size() == 2U);
  cpu.reset();
  assets.collect_unused();
  check(assets.cache().size() == 1U && render.owns(material));
  material.reset();
  assets.collect_unused();
  check(assets.cache().size() == 0U && resources.live_resource_count() == 0U);
}

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
  check(vfs.mount("asset://", files) == gneiss::engine::core::result::success);
  engine::render::render_resource_service resources;
  engine::resource::asset_manager active_assets(vfs);
  engine::resource::resource_cache active_cache, candidate_cache;
  engine::render::render_asset_loader active_render(vfs, active_cache, resources);
  engine::render::render_asset_loader candidate_render(vfs, candidate_cache, resources);
  engine::resource::asset_manager candidate_assets(vfs);
  {
    engine::render::render_asset_ref first, second;
    engine::render::asset_diagnostic diagnostic;
    check(active_render.acquire_mesh("asset://m.mesh", first, diagnostic) ==
          gneiss::engine::core::result::success);
    check(active_render.acquire_mesh("asset://m.mesh", second, diagnostic) ==
          gneiss::engine::core::result::success);
    check(gneiss::engine::render::asset_rid(first) == gneiss::engine::render::asset_rid(second));
    active_assets.collect_unused();
    active_render.release_unused();
    check(resources.get_mesh(gneiss::engine::render::asset_rid(first)) != nullptr);
    const auto render_count = resources.live_resource_count();
    engine::resource::prefab_asset_ref prefab;
    engine::resource::scene_diagnostic prefab_diagnostic;
    check(active_assets.acquire("asset://p.prefab", prefab, prefab_diagnostic) ==
          gneiss::engine::core::result::success);
    check(prefab.get() != nullptr);
    check(resources.live_resource_count() == render_count);
    active_assets.collect_unused();
    check(resources.get_mesh(gneiss::engine::render::asset_rid(first)) != nullptr);
    engine::render::render_asset_ref invalid;
    check(!active_render.owns(invalid));
    engine::render::render_resource_service foreign_resources;
    engine::resource::resource_cache foreign_cache;
    engine::render::render_asset_loader foreign_assets(vfs, foreign_cache, foreign_resources);
    engine::render::render_asset_ref foreign_mesh;
    check(foreign_assets.acquire_mesh("asset://m.mesh", foreign_mesh, diagnostic) ==
          gneiss::engine::core::result::success);
    check(!active_render.owns(foreign_mesh));
  }
  auto active = std::make_unique<runtime::scene_domain>();
  check(active->native_world() == nullptr && active->world() == GNEISS_NULL_WORLD &&
        active->scenes() == nullptr);
  check(active->initialize(active_assets, active_render) == gneiss::engine::core::result::success);
  const auto initialized_world = active->world();
  auto* initialized_scenes = active->scenes();
  check(active->initialize(active_assets, active_render) ==
        gneiss::engine::core::result::invalid_state);
  check(active->world() == initialized_world && active->scenes() == initialized_scenes);

  gneiss_scene_instance old_scene{};
  check(active->scenes()->load("asset://s.scene", &old_scene) ==
        gneiss::engine::core::result::success);
  engine::scene::scene_node_view old_info;
  check(active->scenes()->get_node_info(old_scene, 0U, old_info) ==
        gneiss::engine::core::result::success);
  engine::render::render_asset_ref old_material;
  engine::render::asset_diagnostic diagnostic;
  check(active_render.acquire_material("asset://m.material", old_material, diagnostic) ==
        gneiss::engine::core::result::success);
  auto old_pixels = resources.share_material(gneiss::engine::render::asset_rid(old_material));
  check(old_pixels && old_pixels->red == 1.0F);
  files->files["m.material"] = R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  const auto empty_instances = files->files["s.scene"].find("\"prefab_instances\":[]");
  files->files["s.scene"].replace(
      empty_instances, 21U,
      R"("prefab_instances":[{"instance_uuid":"00000000-0000-4000-8000-000000000004","prefab":"asset://p.prefab","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"overrides":[]}])");
  engine::scene::prepared_scene_description prepared;
  engine::resource::scene_diagnostic scene_error;
  check(engine::scene::prepare_scene_description(vfs, "asset://s.scene", prepared, scene_error,
                                                 {}) == gneiss::engine::core::result::success);
  auto candidate = std::make_unique<runtime::scene_domain>();
  check(candidate->initialize(candidate_assets, candidate_render) ==
        gneiss::engine::core::result::success);
  std::vector<engine::render::render_asset_ref> captured{engine::render::render_asset_ref{}};
  check(candidate->scenes()->capture_render_assets(captured) ==
            gneiss::engine::core::result::success &&
        captured.empty());
  check(!engine::render::render_asset_ref{});
  check(!engine::render::render_asset_ref{});
  const std::array requests{engine::render::render_asset_reload{
                                "asset://m.mesh", engine::render::render_asset_type::mesh},
                            engine::render::render_asset_reload{
                                "asset://m.material", engine::render::render_asset_type::material}};
  engine::render::prepared_render_batch batch;
  check(engine::render::prepare_render_assets(vfs, requests, batch, diagnostic, {}) ==
        gneiss::engine::core::result::success);
  std::vector<engine::render::render_asset_loader::asset_upload> staged;
  for (auto& asset : batch.assets) {
    engine::render::render_asset_loader::asset_upload value;
    check(candidate_render.prepare_upload(std::move(asset), value) ==
          gneiss::engine::core::result::success);
    staged.push_back(std::move(value));
  }
  for (auto& value : staged) {
    check(candidate_render.complete_upload(value) == gneiss::engine::core::result::success);
  }
  engine::scene::scene_instances::asset_bindings bindings;
  for (const auto& value : staged) {
    bindings.emplace(std::string(gneiss::engine::render::asset_uri(value.target)), value.target);
  }
  auto material =
      engine::render::render_asset_loader::material_ref(bindings.at("asset://m.material"));
  const auto mesh_rid = gneiss::engine::render::asset_rid(bindings.at("asset://m.mesh"));
  check(material && gneiss::engine::render::asset_rid(material) !=
                        gneiss::engine::render::asset_rid(old_material));
  check(resources.get_material(gneiss::engine::render::asset_rid(material))->green == 1.0F);
  check(!engine::render::render_asset_loader::mesh_ref(bindings.at("asset://m.material")));
  check(!engine::render::render_asset_loader::material_ref(bindings.at("asset://m.mesh")));
  engine::render::render_asset_ref uncached;
  check(candidate_render.acquire_cached(requests.front(), uncached) ==
        gneiss::engine::core::result::success);
  // 绑定复用稳定资源；另一个 URI 的蓝色材质不影响它。
  files->files["blue.material"] = R"({"format":"gneiss.material","version":1,"color":[0,0,1,1]})";
  engine::render::render_asset_ref unrelated;
  check(candidate_render.acquire_material("asset://blue.material", unrelated, diagnostic) ==
        gneiss::engine::core::result::success);
  check(gneiss::engine::render::asset_rid(unrelated) !=
        gneiss::engine::render::asset_rid(material));
  staged.clear();
  const auto reads = files->reads;
  gneiss_scene_instance new_scene{};
  check(candidate->scenes()->instantiate_prepared(std::move(prepared), bindings, new_scene) ==
        gneiss::engine::core::result::success);
  check(new_scene != GNEISS_NULL_SCENE_INSTANCE);
  std::uint64_t unchanged_count{};
  check(gneiss::engine::core::result{gneiss_world_entity_count(
            active->world(), &unchanged_count)} == gneiss::engine::core::result::success &&
        unchanged_count == 1U);
  check(resources.get_material(gneiss::engine::render::asset_rid(old_material))->red == 1.0F);
  check(files->reads == reads);
  // 候选仅持有一份运行时；移动、失败和取消不得泄漏或提前发布。
  {
    engine::resource::prefab_asset_ref source;
    engine::resource::prefab_instance_description author;
    check(candidate->scenes()->capture_prefab_header(
              new_scene, "00000000-0000-4000-8000-000000000004", author, source) ==
          gneiss::engine::core::result::success);
    const auto original_uuid = author.instance_uuid;
    const auto* original_source = source.get();
    check(candidate->scenes()->capture_prefab_header(new_scene, "missing", author, source) ==
          gneiss::engine::core::result::not_found);
    check(author.instance_uuid == original_uuid && source.get() == original_source);
    bool removal_complete = true;
    check(candidate->scenes()->prepare_prefab_removal(new_scene, "missing", removal_complete) ==
              gneiss::engine::core::result::not_found &&
          !removal_complete);
    author.instance_uuid = "00000000-0000-4000-8000-000000000077";
    author.prefab_uri = "asset://p.prefab";
    author.rotation = {0, 0, 0, 1};
    author.scale = {1, 1, 1};
    engine::scene::prefab_candidate pending;
    const auto transaction_reads = files->reads;
    check(candidate->scenes()->begin_prefab_candidate(new_scene, author, source, *source.get(),
                                                      pending) ==
          gneiss::engine::core::result::success);
    const auto hidden_root = pending.root();
    check(candidate->scenes()->begin_prefab_candidate(new_scene, author, source, *source.get(),
                                                      pending) ==
          gneiss::engine::core::result::invalid_state);
    check(pending.root() == hidden_root);
    check(candidate->scenes()->commit_prefab_removal(new_scene, original_uuid, pending) ==
          gneiss::engine::core::result::invalid_state);
    check(pending.root() == hidden_root);
    engine::scene::prefab_candidate moved{std::move(pending)};
    check(!pending.is_valid() && moved.is_valid());
    check(candidate->scenes()->commit_prefab_candidate(old_scene, moved) ==
          gneiss::engine::core::result::invalid_handle);
    check(candidate->scenes()->commit_prefab_candidate(new_scene, moved) ==
          gneiss::engine::core::result::invalid_state);
    gneiss::engine::core::result foreign{};
    bool cleaned_on_worker = true;
    gneiss::engine::core::result foreign_commit{};
    std::thread worker([&] {
      engine::scene::prefab_node_view preview;
      foreign = moved.preview(0, preview);
      foreign_commit = candidate->scenes()->commit_prefab_candidate(new_scene, moved);
      cleaned_on_worker = moved.cleanup_step(1);
    });
    worker.join();
    check(foreign == gneiss::engine::core::result::invalid_state && !cleaned_on_worker &&
          foreign_commit == gneiss::engine::core::result::invalid_state);
    engine::scene::prefab_candidate assigned;
    assigned = std::move(moved);
    check(!moved.is_valid() && assigned.is_valid());
    for (unsigned step = 0; step < 8 && assigned.is_valid(); ++step)
      (void)assigned.cleanup_step(1);
    check(!assigned.is_valid() && assigned.cleanup_step(1));
    engine::core::math::transform hidden;
    check(candidate->native_world()->node_get_local_transform(scene_node_id{hidden_root}, hidden) ==
          gneiss::engine::core::result::invalid_handle);
    std::uint64_t count{};
    check(candidate->native_world()->entity_count(count) == gneiss::engine::core::result::success &&
          count == 3);
    check(files->reads == transaction_reads);
  }
  const auto selected_material = gneiss::engine::render::asset_rid(material);
  check(candidate->scenes()->capture_render_assets(captured) ==
        gneiss::engine::core::result::success);
  check(captured.size() == 2U && files->reads == reads);
  const auto has_rid = [&](std::uint64_t rid) {
    return std::ranges::any_of(captured, [rid](const auto& lease) {
      return gneiss::engine::render::asset_rid(lease) == rid;
    });
  };
  check(has_rid(mesh_rid) && has_rid(selected_material) &&
        !has_rid(gneiss::engine::render::asset_rid(unrelated)));
  // 多个场景共享相同 URI 的资源，按 RID 去重。
  gneiss_scene_instance additional{};
  check(candidate->scenes()->load("asset://s.scene", &additional) ==
        gneiss::engine::core::result::success);
  check(candidate->scenes()->capture_render_assets(captured) ==
        gneiss::engine::core::result::success);
  check(captured.size() == 2U && has_rid(selected_material) &&
        !has_rid(gneiss::engine::render::asset_rid(unrelated)));
  gneiss_scene_instance duplicate_scene{};
  check(candidate->scenes()->load("asset://s.scene", &duplicate_scene) ==
        gneiss::engine::core::result::success);
  check(candidate->scenes()->capture_render_assets(captured) ==
        gneiss::engine::core::result::success);
  check(captured.size() == 2U && has_rid(selected_material) &&
        !has_rid(gneiss::engine::render::asset_rid(unrelated)));
  check(candidate->scenes()->unload(duplicate_scene) == gneiss::engine::core::result::success);
  check(candidate->scenes()->unload(additional) == gneiss::engine::core::result::success);
  check(candidate->scenes()->capture_render_assets(captured) ==
        gneiss::engine::core::result::success);
  check(captured.size() == 2U && !has_rid(gneiss::engine::render::asset_rid(unrelated)));
  gneiss::engine::core::result snapshot_thread = gneiss::engine::core::result::success;
  std::thread snapshot_worker(
      [&] { snapshot_thread = candidate->scenes()->capture_render_assets(captured); });
  snapshot_worker.join();
  check(snapshot_thread == gneiss::engine::core::result::invalid_state && captured.size() == 2U &&
        has_rid(selected_material));
  material = {};
  candidate_render.release_unused();
  check(resources.get_material(selected_material) != nullptr &&
        resources.get_material(selected_material)->green == 1.0F);
  engine::render::render_asset_ref next_version;
  check(candidate_render.acquire_cached(requests.back(), next_version) ==
        gneiss::engine::core::result::success);
  engine::scene::scene_node_view ordinary;
  engine::scene::prefab_node_view prefab_node;
  check(candidate->scenes()->get_node_info(new_scene, 0U, ordinary) ==
        gneiss::engine::core::result::success);
  check(candidate->scenes()->get_prefab_node_info(new_scene, 1U, prefab_node) ==
        gneiss::engine::core::result::success);
  const gneiss::engine::world::mesh_renderer expected{.mesh = mesh_rid,
                                                      .material = selected_material};
  const std::array unchanged{engine::world::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = expected, .replacement = expected}};
  const auto edit_reads = files->reads;
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://m.material") ==
        gneiss::engine::core::result::success);
  check(files->reads == edit_reads);
  check(engine::world::replace_mesh_renderers(candidate->world(), unchanged) ==
        gneiss::engine::core::result::success);
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://missing.material") ==
        gneiss::engine::core::result::not_found);
  check(engine::world::replace_mesh_renderers(candidate->world(), unchanged) ==
        gneiss::engine::core::result::success);
  files->files["other.material"] = files->files["m.material"];
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://other.material") ==
        gneiss::engine::core::result::success);
  engine::render::render_asset_ref edited_material;
  check(candidate_render.acquire_material("asset://other.material", edited_material, diagnostic) ==
        gneiss::engine::core::result::success);
  const gneiss::engine::world::mesh_renderer edited{
      .mesh = mesh_rid, .material = gneiss::engine::render::asset_rid(edited_material)};
  const std::array material_only{engine::world::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = edited, .replacement = edited}};
  check(engine::world::replace_mesh_renderers(candidate->world(), material_only) ==
        gneiss::engine::core::result::success);
  check(resources.get_material(gneiss::engine::render::asset_rid(old_material))->red == 1.0F);
  candidate_render.release_unused();
  check(resources.get_material(selected_material) != nullptr);
  captured.clear();
  candidate_render.release_unused();
  check(resources.get_material(selected_material) != nullptr);
  {
    using binding = engine::scene::scene_instances::mesh_binding;
    binding before;
    const auto reads = files->reads;
    check(candidate->scenes()->capture_mesh_binding(new_scene, ordinary.node, before) ==
          gneiss::engine::core::result::success);
    binding after{.mesh = before.mesh, .material = next_version};
    auto invalid = after;
    invalid.material = after.mesh;
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, invalid) ==
          gneiss::engine::core::result::invalid_argument);
    gneiss::engine::core::result worker_result{};
    std::thread edit_worker([&] {
      worker_result =
          candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after);
    });
    edit_worker.join();
    check(worker_result == gneiss::engine::core::result::invalid_state);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after) ==
          gneiss::engine::core::result::success);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after) ==
          gneiss::engine::core::result::invalid_state);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, after, before) ==
          gneiss::engine::core::result::success);
    const engine::scene::mesh_renderer_node_creation creation{
        .uuid = "00000000-0000-4000-8000-000000000099",
        .name = "prepared",
        .parent = {},
        .mesh_uri = gneiss::engine::render::asset_uri(before.mesh),
        .material_uri = gneiss::engine::render::asset_uri(before.material),
    };
    gneiss::scene_node_id created{};
    check(candidate->scenes()->create_mesh_renderer_node_ready(new_scene, creation, invalid,
                                                               created) ==
              gneiss::engine::core::result::invalid_argument &&
          !created.is_valid());
    check(candidate->scenes()->create_mesh_renderer_node_ready(
              new_scene, creation, before, created) == gneiss::engine::core::result::success);
    const auto created_node = created;
    check(candidate->scenes()->create_mesh_renderer_node_ready(new_scene, creation, before,
                                                               created) ==
              gneiss::engine::core::result::invalid_argument &&
          !created.is_valid());
    check(candidate->scenes()->destroy_node(new_scene, gneiss::scene_node_id{created_node}) ==
          gneiss::engine::core::result::success);
    check(files->reads == reads);
  }
  check(candidate->scenes()->remove_mesh_renderer(new_scene, ordinary.node) ==
        gneiss::engine::core::result::success);
  const auto snapshot_reads = files->reads;
  check(candidate->scenes()->capture_render_assets(captured) ==
        gneiss::engine::core::result::success);
  check(captured.size() == 2U && has_rid(mesh_rid) &&
        has_rid(gneiss::engine::render::asset_rid(next_version)));
  check(files->reads == snapshot_reads);
  captured.clear();
  const auto old_world = active->world();
  active.swap(candidate);
  std::uint64_t count{};
  check(gneiss::engine::core::result{gneiss_world_entity_count(active->world(), &count)} ==
            gneiss::engine::core::result::success &&
        count == 3U);
  check(active->scenes()->get_node_count(old_scene, &count) ==
        gneiss::engine::core::result::invalid_handle);
  check(active->scenes()->get_node_count(new_scene, &count) ==
            gneiss::engine::core::result::success &&
        count == 1U);
  old_material = {};
  candidate.reset();
  check(gneiss::engine::core::result{gneiss_world_entity_count(old_world, &count)} ==
        gneiss::engine::core::result::invalid_handle);
  check(old_pixels->red == 1.0F);

  // 构造失败时仅回收候选节点；即使依赖缺失也不能退回主线程文件读取。
  check(engine::scene::prepare_scene_description(vfs, "asset://s.scene", prepared, scene_error,
                                                 {}) == gneiss::engine::core::result::success);
  candidate = std::make_unique<runtime::scene_domain>();
  check(candidate->initialize(candidate_assets, candidate_render) ==
        gneiss::engine::core::result::success);
  const auto failure_reads = files->reads;
  gneiss_scene_instance failed_instance{};
  check(candidate->scenes()->instantiate_prepared(std::move(prepared), {}, failed_instance) ==
        gneiss::engine::core::result::not_ready);
  check(failed_instance == GNEISS_NULL_SCENE_INSTANCE);
  check(files->reads == failure_reads);
  check(gneiss::engine::core::result{gneiss_world_entity_count(candidate->world(), &count)} ==
            gneiss::engine::core::result::success &&
        count == 0U);
  check(gneiss::engine::core::result{gneiss_world_entity_count(active->world(), &count)} ==
            gneiss::engine::core::result::success &&
        count == 3U);
}
} // namespace

int main() try {
  unified_resource_index();
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
