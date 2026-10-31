// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_render.hpp"
#include "../support/async_scene.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/prefab_query.hpp"
#include "../support/prepare_render.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/function/world/world.hpp"
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
struct memory_files final : gneiss::test_support::memory_file_fixture {
  std::map<std::string, std::string> files;
  mutable unsigned reads{};
  gneiss::engine::error read(std::string_view path,
                             std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss::engine::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& bytes) const noexcept override try {
    ++reads;
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::error::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::error::invalid_argument;
    }
    bytes.resize(found->second.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(found->second[i]);
    }
    return gneiss::engine::error::success;
  } catch (...) {
    return gneiss::engine::error::out_of_memory;
  }
};
constexpr std::string_view object =
    R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://m.mesh","material":"asset://m.material"}}]})";

void unified_resource_index() {
  using namespace gneiss::engine;
  using gneiss::engine::error;
  class text_loader final : public asset_decoder {
  public:
    gneiss::engine::type_id target_type() const noexcept override {
      return gneiss::engine::runtime_type_of<std::string>();
    }
    result<std::shared_ptr<const void>, meta::serialization_diagnostic>
    decode(const std::vector<std::byte>& bytes) const override {
      return std::shared_ptr<const void>{
          std::make_shared<std::string>(reinterpret_cast<const char*>(bytes.data()), bytes.size())};
    }
  };
  auto source = std::make_shared<memory_files>();
  source->files["same"] = "cpu";
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == error::success);
  gneiss::engine::render_service resources;
  gneiss::engine::resource_service::implementation resources_factory{resources};
  asset_manager assets(gneiss::test_support::test_files(files));
  resource_service render{assets};
  if (render.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(assets.register_decoder(std::make_shared<text_loader>()) == error::success);
  asset_ref<const std::string> cpu;
  check(gneiss::test_support::load_asset(assets, "asset://same", cpu) == error::success);
  prefab_description description;
  description.objects.emplace_back();
  prefab_asset_ref prefab;
  check(gneiss::test_support::assign_value(
            assets.store<gneiss::engine::prefab_description>(
                "asset://same",
                std::make_shared<const gneiss::engine::prefab_description>(std::move(description))),
            prefab) == error::success);
  gneiss::engine::render_asset_ref material;
  check(gneiss::test_support::take_value(
            render.reserve_asset({"asset://same", gneiss::engine::asset_type::material}),
            material) == error::success);
  assets.collect_unused();
  check(assets.cache().size() == 2U && render.cache().size() == 1U && *cpu == "cpu" &&
        resources.live_resource_count() == 1U);
  prefab.reset();
  assets.collect_unused();
  check(assets.cache().size() == 1U && render.cache().size() == 1U);
  cpu.reset();
  assets.collect_unused();
  check(assets.cache().size() == 0U && render.owns(material));
  material.reset();
  render.collect_unused();
  check(assets.cache().size() == 0U && resources.live_resource_count() == 0U);
}

void run() {
  auto files = std::make_shared<memory_files>();
  files->files["s.scene"] =
      R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)" +
      std::string(object) + R"(]})";
  files->files["p.prefab"] =
      R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"00000000-0000-4000-8000-000000000003","root_uuid":"00000000-0000-4000-8000-000000000002","objects":[)" +
      std::string(object) + "]}";
  files->files["m.mesh"] =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})";
  files->files["m.material"] =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
  gneiss::test_support::fixture_files vfs;
  check(vfs.mount("asset://", files) == gneiss::engine::error::success);
  gneiss::engine::render_service resources;
  gneiss::engine::resource_service::implementation resources_factory{resources};
  auto& executor = gneiss::test_support::render_tasks();
  gneiss::engine::asset_manager active_assets(gneiss::test_support::test_files(vfs));
  active_assets.attach_task_executor(executor);
  gneiss::engine::asset_manager candidate_assets(gneiss::test_support::test_files(vfs));
  gneiss::engine::resource_service active_render{active_assets};
  if (active_render.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  gneiss::engine::resource_service candidate_render{candidate_assets};
  if (candidate_render.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  {
    gneiss::engine::render_asset_ref first, second;
    gneiss::engine::asset_diagnostic diagnostic;
    check(gneiss::test_support::load_render(active_render, gneiss::engine::asset_type::mesh,
                                            "asset://m.mesh", first,
                                            diagnostic) == gneiss::engine::error::success);
    check(gneiss::test_support::load_render(active_render, gneiss::engine::asset_type::mesh,
                                            "asset://m.mesh", second,
                                            diagnostic) == gneiss::engine::error::success);
    check(gneiss::engine::asset_rid(first) == gneiss::engine::asset_rid(second));
    active_assets.collect_unused();
    active_render.collect_unused();
    check(resources.get_mesh(gneiss::engine::asset_rid(first)) != nullptr);
    const auto render_count = resources.live_resource_count();
    gneiss::engine::prefab_asset_ref prefab;
    gneiss::engine::scene_diagnostic prefab_diagnostic;
    check(gneiss::test_support::load_asset(active_assets, "asset://p.prefab", prefab) ==
          gneiss::engine::error::success);
    check(prefab.get() != nullptr);
    check(resources.live_resource_count() == render_count);
    active_assets.collect_unused();
    check(resources.get_mesh(gneiss::engine::asset_rid(first)) != nullptr);
    gneiss::engine::render_asset_ref invalid;
    check(!active_render.owns(invalid));
    gneiss::engine::render_service foreign_resources;

    gneiss::engine::asset_manager foreign_owner(gneiss::test_support::test_files(vfs));
    gneiss::engine::resource_service foreign_assets{foreign_owner};
    if (foreign_assets.attach_render(foreign_resources).failed())
      throw std::runtime_error("资源管理器渲染绑定失败");
    gneiss::engine::render_asset_ref foreign_mesh;
    check(gneiss::test_support::load_render(foreign_assets, gneiss::engine::asset_type::mesh,
                                            "asset://m.mesh", foreign_mesh,
                                            diagnostic) == gneiss::engine::error::success);
    check(!active_render.owns(foreign_mesh));
  }
  auto active = std::make_unique<runtime::scene_domain>();
  check(active->native_world() == nullptr && active->native_world() == nullptr &&
        active->scenes() == nullptr);
  check(active->initialize(active_render) == gneiss::engine::error::success);
  const auto initialized_world = active->native_world();
  auto* initialized_scenes = active->scenes();
  check(active->initialize(active_render) == gneiss::engine::error::invalid_state);
  check(active->native_world() == initialized_world && active->scenes() == initialized_scenes);

  gneiss::scene_instance_id old_scene{};
  engine::prepared_scene_description initial;
  engine::scene_diagnostic initial_error;
  check(test_support::prepare_scene_for_test(active_assets, executor, false, "asset://s.scene",
                                             initial, initial_error, {})
            .ok());
  engine::scene_instances::asset_bindings initial_bindings;
  for (const auto& request : initial.assets) {
    engine::render_asset_ref reference;
    engine::asset_diagnostic error;
    check(gneiss::test_support::load_render(active_render, request.type, request.uri, reference,
                                            error)
              .ok());
    initial_bindings.emplace(std::string(engine::asset_uri(reference)), reference);
  }
  check(
      active->scenes()->instantiate_prepared(std::move(initial), initial_bindings, old_scene).ok());
  initial_bindings.clear();
  gneiss::engine::scene_node_view old_info;
  check(active->scenes()->get_node_info(old_scene, 0U, old_info) == gneiss::engine::error::success);
  gneiss::engine::render_asset_ref old_material;
  gneiss::engine::asset_diagnostic diagnostic;
  check(gneiss::test_support::load_render(active_render, gneiss::engine::asset_type::material,
                                          "asset://m.material", old_material,
                                          diagnostic) == gneiss::engine::error::success);
  auto old_pixels = resources.share_material(gneiss::engine::asset_rid(old_material));
  check(old_pixels && old_pixels->parameters.color[0] == 1.0F);
  files->files["m.material"] =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  files->files["s.scene"].insert(
      files->files["s.scene"].size() - 2U,
      R"(,{"uuid":"00000000-0000-4000-8000-000000000004","prefab":{"uri":"asset://p.prefab","overrides":[]},"parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})");
  gneiss::engine::prepared_scene_description prepared;
  gneiss::engine::scene_diagnostic scene_error;
  check(gneiss::test_support::prepare_scene_for_test(active_assets, executor, false,
                                                     "asset://s.scene", prepared, scene_error,
                                                     {}) == gneiss::engine::error::success);
  auto candidate = std::make_unique<runtime::scene_domain>();
  check(candidate->initialize(candidate_render) == gneiss::engine::error::success);
  check(!gneiss::engine::render_asset_ref{});
  check(!gneiss::engine::render_asset_ref{});
  const std::array requests{
      gneiss::engine::asset_source{"asset://m.mesh", gneiss::engine::asset_type::mesh},
      gneiss::engine::asset_source{"asset://m.material", gneiss::engine::asset_type::material}};
  gneiss::engine::prepared_batch batch;
  check(gneiss::test_support::prepare_render_for_test(candidate_assets, vfs, requests, batch,
                                                      diagnostic,
                                                      {}) == gneiss::engine::error::success);
  gneiss::engine::scene_instances::asset_bindings bindings;
  for (auto& asset : batch.assets) {
    gneiss::engine::render_asset_ref target;
    check(gneiss::test_support::adopt_render_for_test(candidate_render, std::move(asset), target)
              .ok());
    bindings.emplace(std::string(gneiss::engine::asset_uri(target)), target);
  }
  auto material = gneiss::engine::asset_cast<gneiss::engine::material_resource>(
      bindings.at("asset://m.material"));
  const auto mesh_rid = gneiss::engine::asset_rid(bindings.at("asset://m.mesh"));
  check(material && gneiss::engine::asset_rid(material) != gneiss::engine::asset_rid(old_material));
  check(resources.get_material(gneiss::engine::asset_rid(material))->parameters.color[1] == 1.0F);
  check(!gneiss::engine::asset_cast<gneiss::engine::mesh_resource>(
      bindings.at("asset://m.material")));
  check(!gneiss::engine::asset_cast<gneiss::engine::material_resource>(
      bindings.at("asset://m.mesh")));
  gneiss::engine::render_asset_ref uncached;
  check(gneiss::test_support::take_value(candidate_render.find(requests.front()), uncached) ==
        gneiss::engine::error::success);
  // 绑定复用稳定资源；另一个 URI 的蓝色材质不影响它。
  files->files["blue.material"] =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["","","","",""]})";
  gneiss::engine::render_asset_ref unrelated;
  check(gneiss::test_support::load_render(candidate_render, gneiss::engine::asset_type::material,
                                          "asset://blue.material", unrelated,
                                          diagnostic) == gneiss::engine::error::success);
  check(gneiss::engine::asset_rid(unrelated) != gneiss::engine::asset_rid(material));
  const auto reads = files->reads;
  gneiss::scene_instance_id new_scene{};
  // 缺少来源引用必须拒绝，不能通过临时描述回写缓存。
  auto missing_source = prepared;
  missing_source.prefabs.begin()->second.resource.reset();
  gneiss::scene_instance_id rejected_scene{};
  check(candidate->scenes()->instantiate_prepared(std::move(missing_source), bindings,
                                                  rejected_scene) ==
            gneiss::engine::error::invalid_argument &&
        rejected_scene == gneiss::scene_instance_id{});
  check(candidate->scenes()->instantiate_prepared(std::move(prepared), bindings, new_scene) ==
        gneiss::engine::error::success);
  check(new_scene != gneiss::scene_instance_id{});
  std::uint64_t unchanged_count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active->native_world()->entity_count(), unchanged_count)} ==
            gneiss::engine::error::success &&
        unchanged_count == 1U);
  check(resources.get_material(gneiss::engine::asset_rid(old_material))->parameters.color[0] ==
        1.0F);
  check(files->reads == reads);
  // 同步创建不读文件；替换失败删除旧实例并清理新建残留。
  {
    gneiss::engine::prefab_asset_ref source;
    gneiss::engine::object_description author;
    check(candidate->scenes()
              ->capture_prefab_header(
                  new_scene, *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000004"),
                  author, source)
              .ok());
    std::vector<gneiss::engine::scene_mesh_binding> bindings(source.get()->objects.size());
    for (std::size_t i = 0; i < bindings.size(); ++i)
      check(
          candidate->scenes()->capture_prefab_binding(new_scene, author.uuid, i, bindings[i]).ok());
    author.uuid = *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000077");
    const auto reads_before = files->reads;
    std::uint64_t baseline{};
    check(gneiss::test_support::assign_value(candidate->native_world()->entity_count(), baseline)
              .ok());
    gneiss::scene_node_id root;
    gneiss::engine::error foreign;
    std::thread worker([&] {
      foreign = candidate->scenes()->instantiate_prefab_ready(new_scene, author, source, bindings,
                                                              false, root);
    });
    worker.join();
    check(foreign == gneiss::engine::error::invalid_state && !root);
    check(candidate->scenes()->instantiate_prefab_ready(old_scene, author, source, bindings, false,
                                                        root) ==
          gneiss::engine::error::invalid_handle);
    check(candidate->scenes()
              ->instantiate_prefab_ready(new_scene, author, source, bindings, false, root)
              .ok());
    const auto previous_root = root;
    gneiss::engine::math::transform queried;
    check(candidate->native_world()->node_get_local_transform(root, queried).ok());
    check(candidate->scenes()
              ->instantiate_prefab_ready(new_scene, author, source, bindings, true, root)
              .ok());
    check(root != previous_root);
    check(candidate->native_world()->node_get_local_transform(previous_root, queried) ==
          gneiss::engine::error::invalid_handle);
    const auto replaced_root = root;
    // 根变换非法使创建在旧实例已删除后失败。
    author.transform.scale = {0, 1, 1};
    check(candidate->scenes()
              ->instantiate_prefab_ready(new_scene, author, source, bindings, true, root)
              .failed());
    check(!root && candidate->native_world()->node_get_local_transform(replaced_root, queried) ==
                       gneiss::engine::error::invalid_handle);
    std::uint64_t count{};
    check(
        gneiss::test_support::assign_value(candidate->native_world()->entity_count(), count).ok() &&
        count == baseline);
    check(candidate->scenes()->remove_prefab_instance(new_scene, author.uuid) ==
          gneiss::engine::error::not_found);
    check(files->reads == reads_before);
  }
  const auto selected_material = gneiss::engine::asset_rid(material);
  // 多个场景共享相同 URI 的资源身份。
  gneiss::scene_instance_id additional{};
  check(candidate->scenes()->capture_prepared(new_scene, prepared, bindings).ok());
  check(candidate->scenes()->instantiate_prepared(std::move(prepared), bindings, additional).ok());
  gneiss::scene_instance_id duplicate_scene{};
  check(candidate->scenes()->capture_prepared(new_scene, prepared, bindings).ok());
  check(candidate->scenes()
            ->instantiate_prepared(std::move(prepared), bindings, duplicate_scene)
            .ok());
  check(candidate->scenes()->unload(duplicate_scene) == gneiss::engine::error::success);
  check(candidate->scenes()->unload(additional) == gneiss::engine::error::success);
  material = {};
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr &&
        resources.get_material(selected_material)->parameters.color[1] == 1.0F);
  gneiss::engine::render_asset_ref next_version;
  check(gneiss::test_support::take_value(candidate_render.find(requests.back()), next_version) ==
        gneiss::engine::error::success);
  gneiss::engine::scene_node_view ordinary;
  gneiss::engine::scene_node_view prefab_node;
  check(candidate->scenes()->get_node_info(new_scene, 0U, ordinary) ==
        gneiss::engine::error::success);
  check(gneiss::test_support::prefab_node_info(*candidate->scenes(), new_scene, 0U, prefab_node) ==
        gneiss::engine::error::success);
  const gneiss::engine::mesh_renderer expected{
      .mesh = candidate_render.find_mesh(mesh_rid),
      .material = candidate_render.find_material(selected_material)};
  const std::array unchanged{gneiss::engine::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = expected, .replacement = expected}};
  const auto edit_reads = files->reads;
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://m.material") ==
        gneiss::engine::error::success);
  check(files->reads == edit_reads);
  check(candidate->native_world()->replace_mesh_renderers(unchanged) ==
        gneiss::engine::error::success);
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://missing.material") ==
        gneiss::engine::error::not_found);
  check(candidate->native_world()->replace_mesh_renderers(unchanged) ==
        gneiss::engine::error::success);
  files->files["other.material"] = files->files["m.material"];
  gneiss::engine::render_asset_ref edited_material;
  check(gneiss::test_support::load_render(candidate_render, gneiss::engine::asset_type::material,
                                          "asset://other.material", edited_material,
                                          diagnostic) == gneiss::engine::error::success);
  check(candidate->scenes()->set_mesh_renderer(new_scene, ordinary.node, "asset://m.mesh",
                                               "asset://other.material") ==
        gneiss::engine::error::success);
  const gneiss::engine::mesh_renderer edited{
      .mesh = candidate_render.find_mesh(mesh_rid),
      .material = edited_material->as<gneiss::engine::material_resource>()};
  const std::array material_only{gneiss::engine::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = edited, .replacement = edited}};
  check(candidate->native_world()->replace_mesh_renderers(material_only) ==
        gneiss::engine::error::success);
  check(resources.get_material(gneiss::engine::asset_rid(old_material))->parameters.color[0] ==
        1.0F);
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr);
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr);
  {
    using binding = gneiss::engine::scene_instances::mesh_binding;
    binding before;
    const auto reads = files->reads;
    check(candidate->scenes()->capture_mesh_binding(new_scene, ordinary.node, before) ==
          gneiss::engine::error::success);
    binding after{.mesh = before.mesh,
                  .material =
                      gneiss::engine::asset_cast<gneiss::engine::material_resource>(next_version)};
    auto invalid = after;
    invalid.material = {};
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, invalid) ==
          gneiss::engine::error::invalid_argument);
    gneiss::engine::error worker_result{};
    std::thread edit_worker([&] {
      worker_result =
          candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after);
    });
    edit_worker.join();
    check(worker_result == gneiss::engine::error::invalid_state);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after) ==
          gneiss::engine::error::success);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, before, after) ==
          gneiss::engine::error::invalid_state);
    check(candidate->scenes()->replace_mesh_binding(new_scene, ordinary.node, after, before) ==
          gneiss::engine::error::success);
    const gneiss::engine::mesh_renderer_node_creation creation{
        .uuid = *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000099"),
        .name = "prepared",
        .parent = {},
        .mesh_uri = gneiss::engine::asset_uri(before.mesh),
        .material_uri = gneiss::engine::asset_uri(before.material),
    };
    gneiss::scene_node_id created{};
    check(candidate->scenes()->create_mesh_renderer_node_ready(
              new_scene, creation, invalid, created) == gneiss::engine::error::invalid_argument &&
          !created.is_valid());
    check(candidate->scenes()->create_mesh_renderer_node_ready(
              new_scene, creation, before, created) == gneiss::engine::error::success);
    const auto created_node = created;
    check(candidate->scenes()->create_mesh_renderer_node_ready(
              new_scene, creation, before, created) == gneiss::engine::error::invalid_argument &&
          !created.is_valid());
    check(candidate->scenes()->destroy_node(new_scene, gneiss::scene_node_id{created_node}) ==
          gneiss::engine::error::success);
    check(files->reads == reads);
  }
  check(candidate->scenes()->remove_mesh_renderer(new_scene, ordinary.node) ==
        gneiss::engine::error::success);
  const auto snapshot_reads = files->reads;
  check(files->reads == snapshot_reads);
  active.swap(candidate);
  std::uint64_t count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active->native_world()->entity_count(), count)} == gneiss::engine::error::success &&
        count == 2U);
  check(active->scenes()->get_node_count(old_scene, &count) ==
        gneiss::engine::error::invalid_handle);
  check(active->scenes()->get_node_count(new_scene, &count) == gneiss::engine::error::success &&
        count == 2U);
  old_material = {};
  candidate.reset();
  // 旧域已析构；此前借用的 World 指针不再可访问。
  check(old_pixels->parameters.color[0] == 1.0F);

  // 构造失败时仅回收候选节点；即使依赖缺失也不能退回主线程文件读取。
  check(gneiss::test_support::prepare_scene_for_test(active_assets, executor, false,
                                                     "asset://s.scene", prepared, scene_error,
                                                     {}) == gneiss::engine::error::success);
  candidate = std::make_unique<runtime::scene_domain>();
  check(candidate->initialize(candidate_render) == gneiss::engine::error::success);
  const auto failure_reads = files->reads;
  gneiss::scene_instance_id failed_instance{};
  check(candidate->scenes()->instantiate_prepared(std::move(prepared), {}, failed_instance) ==
        gneiss::engine::error::not_ready);
  check(failed_instance == gneiss::scene_instance_id{});
  check(files->reads == failure_reads);
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            candidate->native_world()->entity_count(), count)} == gneiss::engine::error::success &&
        count == 0U);
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active->native_world()->entity_count(), count)} == gneiss::engine::error::success &&
        count == 2U);
}
} // namespace

int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  unified_resource_index();
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
