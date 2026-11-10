// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_render.hpp"
#include "../support/async_scene.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/prepare_render.hpp"
#include "../support/scene_application.hpp"
#include "../support/scene_instantiation.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/scene/scene_preparation.hpp"
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
    R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},
"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://m.mesh","material":"asset://m.material","uuid":"ac333749-72c7-4d50-bb9f-1751770a897c"}}]})";

void unified_resource_index() {
  using namespace gneiss::engine;
  using gneiss::engine::error;
  struct text_asset final : asset {
    explicit text_asset(std::string text)
        : asset(runtime_type_of<text_asset>()), content(std::move(text)) {}
    std::string content;
  };
  class text_loader final : public asset_decoder {
  public:
    gneiss::engine::type_id target_type() const noexcept override {
      return gneiss::engine::runtime_type_of<text_asset>();
    }
    result<std::shared_ptr<const void>, serialization::diagnostic>
    decode(const std::vector<std::byte>& bytes) const override {
      return std::shared_ptr<const void>{std::make_shared<text_asset>(
          std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()))};
    }
  };
  auto source = std::make_shared<memory_files>();
  source->files["same"] = "cpu";
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == error::success);
  gneiss::engine::render_service resources;
  gneiss::engine::detail::asset_construction resources_factory{resources};
  asset_manager assets(gneiss::test_support::test_files(files));
  auto& render = assets;
  if (render.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(assets.register_decoder(std::make_shared<text_loader>()) == error::success);
  asset_ref<const text_asset> cpu;
  check(gneiss::test_support::load_asset(assets, "asset://same", cpu) == error::success);
  prefab_description description;
  description.objects.emplace_back();
  prefab_asset_ref prefab{std::make_shared<const prefab_description>(std::move(description))};
  gneiss::engine::render_resource_ref material;
  check(gneiss::test_support::take_value(
            gneiss::test_support::request_resource(
                render, {"asset://same", gneiss::engine::render_resource_kind::material}),
            material) == error::success);
  assets.collect_unused();
  check(assets.cached_count() == 2U && cpu->content == "cpu" &&
        resources.live_resource_count() == 1U);
  prefab.reset();
  assets.collect_unused();
  check(assets.cached_count() == 2U);
  cpu.reset();
  assets.collect_unused();
  check(assets.cached_count() == 1U && render.owns(material));
  material.reset();
  render.collect_unused();
  check(assets.cached_count() == 0U && resources.live_resource_count() == 0U);
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
  gneiss::engine::detail::asset_construction resources_factory{resources};
  auto& executor = gneiss::test_support::render_tasks();
  gneiss::engine::asset_manager active_assets(gneiss::test_support::test_files(vfs));
  active_assets.configure(executor);
  gneiss::engine::asset_manager candidate_assets(gneiss::test_support::test_files(vfs));
  auto& active_render = active_assets;
  if (active_render.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  auto& candidate_render = candidate_assets;
  if (candidate_render.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  {
    gneiss::engine::render_resource_ref first, second;
    gneiss::engine::asset_diagnostic diagnostic;
    check(gneiss::test_support::load_render(
              active_render, gneiss::engine::render_resource_kind::mesh, "asset://m.mesh", first,
              diagnostic) == gneiss::engine::error::success);
    check(gneiss::test_support::load_render(
              active_render, gneiss::engine::render_resource_kind::mesh, "asset://m.mesh", second,
              diagnostic) == gneiss::engine::error::success);
    check(gneiss::engine::resource_rid(first) == gneiss::engine::resource_rid(second));
    active_assets.collect_unused();
    active_render.collect_unused();
    check(resources.get_mesh(gneiss::engine::resource_rid(first)) != nullptr);
    const auto render_count = resources.live_resource_count();
    gneiss::engine::prefab_asset_ref prefab;
    gneiss::engine::scene_diagnostic prefab_diagnostic;
    check(gneiss::test_support::load_scene_snapshot(active_assets, "asset://p.prefab", prefab) ==
          gneiss::engine::error::success);
    check(prefab.get() != nullptr);
    check(resources.live_resource_count() == render_count);
    active_assets.collect_unused();
    check(resources.get_mesh(gneiss::engine::resource_rid(first)) != nullptr);
    gneiss::engine::render_resource_ref invalid;
    check(!active_render.owns(invalid));
    gneiss::engine::render_service foreign_resources;

    gneiss::engine::asset_manager foreign_owner(gneiss::test_support::test_files(vfs));
    auto& foreign_assets = foreign_owner;
    if (foreign_assets.attach_render(foreign_resources).failed())
      throw std::runtime_error("资源管理器渲染绑定失败");
    gneiss::engine::render_resource_ref foreign_mesh;
    check(gneiss::test_support::load_render(
              foreign_assets, gneiss::engine::render_resource_kind::mesh, "asset://m.mesh",
              foreign_mesh, diagnostic) == gneiss::engine::error::success);
    check(!active_render.owns(foreign_mesh));
  }
  auto active = std::make_unique<runtime::scene_domain>();
  check(active->native_world() == nullptr && active->native_world() == nullptr &&
        active->scenes() == nullptr);
  check(active->initialize() == gneiss::engine::error::success);
  const auto initialized_world = active->native_world();
  auto* initialized_scenes = active->scenes();
  check(active->initialize() == gneiss::engine::error::invalid_state);
  check(active->native_world() == initialized_world && active->scenes() == initialized_scenes);

  gneiss::scene_instance_id old_scene{};
  engine::scene_instantiation_data initial;
  engine::scene_diagnostic initial_error;
  check(test_support::prepare_scene_for_test(active_assets, executor, false, "asset://s.scene",
                                             initial, initial_error, {})
            .ok());
  engine::scene_resource_bindings initial_bindings;
  for (const auto& request : initial.assets) {
    engine::render_resource_ref reference;
    engine::asset_diagnostic error;
    check(gneiss::test_support::load_render(active_render, request.type, request.uri, reference,
                                            error)
              .ok());
    initial_bindings.emplace(std::string(engine::resource_uri(reference)), reference);
  }
  // 通用应用只消费显式引用；缺失绑定不得隐式创建资产请求或访问文件。
  {
    auto empty = active->scenes()->create_empty(engine::guid::generate());
    check(bool(empty));
    engine::composed_scene target;
    engine::composed_object object;
    object.address.object = engine::guid::generate();
    object.effective.components.set(engine::mesh_renderer_description{
        "asset://not-requested.mesh", "asset://not-requested.material"});
    target.objects.push_back(object);
    const auto cached = active_assets.cached_count();
    const auto before_reads = files->reads;
    std::map<engine::object_address, engine::scene_object_association> associations;
    auto applied =
        engine::apply_scene(*active->native_world(), *empty, associations, {}, target, {});
    check(!applied && applied.error() == engine::error::not_ready);
    check(active_assets.cached_count() == cached && files->reads == before_reads);
    check(active->scenes()->unload(*empty).ok());
  }
  const auto reads_before_instantiation = files->reads;
  auto direct = gneiss::test_support::scene_fixture::instantiate_scene(
      *active->native_world(), active_assets, initial.source_asset);
  check(bool(direct) && direct->node_count == initial.instance_nodes);
  check(direct->assets.size() == initial.assets.size());
  check(files->reads == reads_before_instantiation);
  old_scene = direct->instance;
  auto second_instance = gneiss::test_support::scene_fixture::instantiate_scene(
      *active->native_world(), active_assets, initial.source_asset);
  check(bool(second_instance) && second_instance->instance != old_scene);
  check(active->scenes()->unload(second_instance->instance).ok());
  check(active->scenes()->get_node_count(old_scene).ok());
  check(gneiss::test_support::scene_fixture::instantiate_scene(*active->native_world(),
                                                               active_assets, {})
            .error()
            .error == engine::error::invalid_argument);
  auto baseline = gneiss::test_support::compose(initial);
  check(bool(baseline));
  auto mapping = gneiss::test_support::associations(direct->objects);
  auto scene_reloaded = engine::apply_scene(*active->native_world(), old_scene, mapping, *baseline,
                                            *baseline, initial_bindings);
  check(scene_reloaded && *scene_reloaded);
  check(files->reads == reads_before_instantiation);
  auto invalid_apply = engine::apply_scene(*active->native_world(), {}, mapping, *baseline,
                                           *baseline, initial_bindings);
  check(!invalid_apply && invalid_apply.error() == engine::error::invalid_handle);
  engine::error foreign_reload;
  std::thread foreign_thread([&] {
    auto result = engine::apply_scene(*active->native_world(), old_scene, mapping, *baseline,
                                      *baseline, initial_bindings);
    foreign_reload = result ? engine::error::success : result.error();
  });
  foreign_thread.join();
  check(foreign_reload == engine::error::invalid_state);
  initial_bindings.clear();
  gneiss::engine::scene_node_info old_info;
  check(gneiss::test_support::take_value(active->scenes()->get_node_info(old_scene, 0U),
                                         old_info) == gneiss::engine::error::success);
  gneiss::engine::render_resource_ref old_material;
  gneiss::engine::asset_diagnostic diagnostic;
  check(gneiss::test_support::load_render(
            active_render, gneiss::engine::render_resource_kind::material, "asset://m.material",
            old_material, diagnostic) == gneiss::engine::error::success);
  auto old_pixels = resources.share_material(gneiss::engine::resource_rid(old_material));
  check(old_pixels && old_pixels->parameters.color[0] == 1.0F);
  files->files["m.material"] =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
  files->files["s.scene"].insert(
      files->files["s.scene"].size() - 2U,
      R"(,{"uuid":"00000000-0000-4000-8000-000000000004","prefab":{"uri":"asset://p.prefab","overrides":[]},"parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})");
  gneiss::engine::scene_instantiation_data prepared;
  gneiss::engine::scene_diagnostic scene_error;
  check(gneiss::test_support::prepare_scene_for_test(active_assets, executor, false,
                                                     "asset://s.scene", prepared, scene_error,
                                                     {}) == gneiss::engine::error::success);
  auto candidate = std::make_unique<runtime::scene_domain>();
  check(candidate->initialize() == gneiss::engine::error::success);
  check(!gneiss::engine::render_resource_ref{});
  check(!gneiss::engine::render_resource_ref{});
  const std::array requests{
      gneiss::engine::render_resource_source{"asset://m.mesh",
                                             gneiss::engine::render_resource_kind::mesh},
      gneiss::engine::render_resource_source{"asset://m.material",
                                             gneiss::engine::render_resource_kind::material}};
  gneiss::engine::prepared_batch batch;
  check(gneiss::test_support::prepare_render_for_test(candidate_assets, vfs, requests, batch,
                                                      diagnostic,
                                                      {}) == gneiss::engine::error::success);
  gneiss::engine::scene_resource_bindings bindings;
  for (auto& asset : batch.assets) {
    gneiss::engine::render_resource_ref target;
    check(gneiss::test_support::adopt_render_for_test(candidate_render, std::move(asset), target)
              .ok());
    bindings.emplace(std::string(gneiss::engine::resource_uri(target)), target);
  }
  auto material =
      gneiss::engine::resource_cast<gneiss::engine::material>(bindings.at("asset://m.material"));
  check(material &&
        gneiss::engine::resource_rid(material) != gneiss::engine::resource_rid(old_material));
  check(resources.get_material(gneiss::engine::resource_rid(material))->parameters.color[1] ==
        1.0F);
  check(!gneiss::engine::resource_cast<gneiss::engine::mesh>(bindings.at("asset://m.material")));
  check(!gneiss::engine::resource_cast<gneiss::engine::material>(bindings.at("asset://m.mesh")));
  gneiss::engine::render_resource_ref uncached;
  check(gneiss::test_support::take_value(candidate_render.find(requests.front()), uncached) ==
        gneiss::engine::error::success);
  // 绑定复用稳定资源；另一个 URI 的蓝色材质不影响它。
  files->files["blue.material"] =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["","","","",""]})";
  gneiss::engine::render_resource_ref unrelated;
  check(gneiss::test_support::load_render(
            candidate_render, gneiss::engine::render_resource_kind::material,
            "asset://blue.material", unrelated, diagnostic) == gneiss::engine::error::success);
  check(gneiss::engine::resource_rid(unrelated) != gneiss::engine::resource_rid(material));
  const auto reads = files->reads;
  gneiss::scene_instance_id new_scene{};
  const auto instantiation_input = prepared;
  auto expected_scene_asset = prepared.source_asset;
  auto expected_prefab_asset = prepared.prefabs.begin()->second.source_asset;
  check(bool(expected_scene_asset) && bool(expected_prefab_asset));
  // 缺少来源引用必须拒绝，不能通过临时描述回写缓存。
  auto missing_source = prepared;
  missing_source.prefabs.begin()->second.resource.reset();
  gneiss::scene_instance_id rejected_scene{};
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_fixture::instantiate_scene(
                *candidate->native_world(), std::move(missing_source), bindings),
            rejected_scene) == gneiss::engine::error::invalid_argument &&
        rejected_scene == gneiss::scene_instance_id{});
  auto created = gneiss::test_support::scene_fixture::instantiate_scene(
      *candidate->native_world(), std::move(prepared), bindings);
  check(bool(created));
  new_scene = created->instance;
  check(new_scene != gneiss::scene_instance_id{});
  // 投影重建使用通用应用：即使输入包含 Prefab，运行节点也不持有来源绑定。
  {
    auto empty = candidate->scenes()->create_empty(engine::guid::generate());
    check(bool(empty));
    auto rebuilt = gneiss::test_support::scene_fixture::instantiate_scene(
        *candidate->native_world(), instantiation_input, bindings);
    check(bool(rebuilt));
    check(candidate->scenes()->unload(*empty).ok());
    check(bool(rebuilt) && rebuilt->instance != *empty &&
          rebuilt->node_count == instantiation_input.instance_nodes);
    bool source_address = false;
    for (const auto& object : rebuilt->objects)
      source_address = source_address || bool(object.source);
    check(source_address);
    for (std::size_t index = 0; index < rebuilt->node_count; ++index) {
      auto info = candidate->scenes()->get_node_info(rebuilt->instance, index);
      check(bool(info));
    }
    check(candidate->scenes()->unload(rebuilt->instance).ok());
    check(files->reads == reads);
  }
  std::uint64_t unchanged_count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active->native_world()->entity_count(), unchanged_count)} ==
            gneiss::engine::error::success &&
        unchanged_count == 1U);
  check(resources.get_material(gneiss::engine::resource_rid(old_material))->parameters.color[0] ==
        1.0F);
  check(files->reads == reads);
  // 同步创建不读文件；失败清理新建成员，不影响已有场景。
  {
    const auto baseline_count = *candidate->native_world()->entity_count();
    engine::error foreign;
    std::thread worker([&] {
      auto result = gneiss::test_support::scene_fixture::instantiate_scene(
          *candidate->native_world(), instantiation_input, bindings);
      foreign = result ? engine::error::success : result.error();
    });
    worker.join();
    check(foreign == engine::error::invalid_state);
    auto broken = instantiation_input;
    broken.description.objects.back().transform.scale = {0, 1, 1};
    check(!gneiss::test_support::scene_fixture::instantiate_scene(*candidate->native_world(),
                                                                  std::move(broken), bindings));
    check(*candidate->native_world()->entity_count() == baseline_count);
    check(files->reads == reads);
  }
  const auto selected_material = gneiss::engine::resource_rid(material);
  // 多个场景共享相同 URI 的资源身份。
  gneiss::scene_instance_id additional{};
  prepared = instantiation_input;
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_fixture::instantiate_scene(*candidate->native_world(),
                                                                   std::move(prepared), bindings),
            additional)
            .ok());
  gneiss::scene_instance_id duplicate_scene{};
  prepared = instantiation_input;
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_fixture::instantiate_scene(*candidate->native_world(),
                                                                   std::move(prepared), bindings),
            duplicate_scene)
            .ok());
  check(candidate->scenes()->unload(duplicate_scene) == gneiss::engine::error::success);
  check(candidate->scenes()->unload(additional) == gneiss::engine::error::success);
  material = {};
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr &&
        resources.get_material(selected_material)->parameters.color[1] == 1.0F);
  gneiss::engine::render_resource_ref next_version;
  check(gneiss::test_support::take_value(candidate_render.find(requests.back()), next_version) ==
        gneiss::engine::error::success);
  gneiss::engine::scene_node_info ordinary;
  gneiss::engine::scene_node_info prefab_node;
  check(gneiss::test_support::take_value(candidate->scenes()->get_node_info(new_scene, 0U),
                                         ordinary) == gneiss::engine::error::success);
  const auto prefab_member = std::ranges::find_if(
      created->objects, [](const auto& object) { return bool(object.source); });
  check(prefab_member != created->objects.end());
  bool found_prefab_member = false;
  for (std::size_t index = 0; index < created->node_count; ++index) {
    auto info = candidate->scenes()->get_node_info(new_scene, index);
    check(bool(info));
    if (info->node == prefab_member->node) {
      prefab_node = *info;
      found_prefab_member = true;
    }
  }
  check(found_prefab_member);
  gneiss::engine::ref<const gneiss::engine::mesh> current_mesh;
  check(gneiss::test_support::take_value(
            candidate_render.find<gneiss::engine::mesh>("asset://m.mesh"), current_mesh)
            .ok());
  const gneiss::engine::mesh_renderer expected{
      .mesh = current_mesh, .material = next_version.as<gneiss::engine::material>()};
  const std::array unchanged{gneiss::engine::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = expected, .replacement = expected}};
  const auto edit_reads = files->reads;
  check(gneiss::engine::set_mesh_renderer(*candidate->native_world(), candidate_assets, new_scene,
                                          ordinary.node, "asset://m.mesh",
                                          "asset://m.material") == gneiss::engine::error::success);
  check(files->reads == edit_reads);
  check(candidate->native_world()->replace_mesh_renderers(unchanged) ==
        gneiss::engine::error::success);
  check(gneiss::engine::set_mesh_renderer(
            *candidate->native_world(), candidate_assets, new_scene, ordinary.node,
            "asset://m.mesh", "asset://missing.material") == gneiss::engine::error::not_found);
  check(candidate->native_world()->replace_mesh_renderers(unchanged) ==
        gneiss::engine::error::success);
  files->files["other.material"] = files->files["m.material"];
  gneiss::engine::render_resource_ref edited_material;
  check(gneiss::test_support::load_render(candidate_render,
                                          gneiss::engine::render_resource_kind::material,
                                          "asset://other.material", edited_material,
                                          diagnostic) == gneiss::engine::error::success);
  check(gneiss::engine::set_mesh_renderer(
            *candidate->native_world(), candidate_assets, new_scene, ordinary.node,
            "asset://m.mesh", "asset://other.material") == gneiss::engine::error::success);
  const gneiss::engine::mesh_renderer edited{
      .mesh = current_mesh, .material = edited_material.as<gneiss::engine::material>()};
  const std::array material_only{gneiss::engine::mesh_renderer_replacement{
      .entity = ordinary.entity, .expected = edited, .replacement = edited}};
  check(candidate->native_world()->replace_mesh_renderers(material_only) ==
        gneiss::engine::error::success);
  check(resources.get_material(gneiss::engine::resource_rid(old_material))->parameters.color[0] ==
        1.0F);
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr);
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr);
  {
    using binding = gneiss::engine::scene_mesh_binding;
    binding before;
    const auto binding_reads = files->reads;
    check(gneiss::test_support::assign_value(
              gneiss::engine::capture_mesh_binding(*candidate->native_world(), new_scene,
                                                   ordinary.node),
              before) == gneiss::engine::error::success);
    binding after{.mesh = before.mesh,
                  .material =
                      gneiss::engine::resource_cast<gneiss::engine::material>(next_version)};
    // 空材质槽位合法；捕获保持为空，随后恢复原绑定。
    auto cleared = before;
    cleared.material = {};
    check(gneiss::engine::replace_mesh_binding(*candidate->native_world(), new_scene, ordinary.node,
                                               before, cleared, {}, &candidate_assets)
              .ok());
    auto empty_binding =
        gneiss::engine::capture_mesh_binding(*candidate->native_world(), new_scene, ordinary.node);
    check(empty_binding && empty_binding->mesh == before.mesh && !empty_binding->material);
    check(gneiss::engine::replace_mesh_binding(*candidate->native_world(), new_scene, ordinary.node,
                                               cleared, before, {}, &candidate_assets)
              .ok());
    gneiss::engine::error worker_result{};
    std::thread edit_worker([&] {
      worker_result =
          gneiss::engine::replace_mesh_binding(*candidate->native_world(), new_scene, ordinary.node,
                                               before, after, {}, &candidate_assets);
    });
    edit_worker.join();
    check(worker_result == gneiss::engine::error::invalid_state);
    check(gneiss::engine::replace_mesh_binding(*candidate->native_world(), new_scene, ordinary.node,
                                               before, after, {}, &candidate_assets) ==
          gneiss::engine::error::success);
    check(gneiss::engine::replace_mesh_binding(*candidate->native_world(), new_scene, ordinary.node,
                                               before, after, {}, &candidate_assets) ==
          gneiss::engine::error::invalid_state);
    check(gneiss::engine::replace_mesh_binding(*candidate->native_world(), new_scene, ordinary.node,
                                               after, before, {}, &candidate_assets) ==
          gneiss::engine::error::success);
    const gneiss::engine::mesh_renderer_node_creation creation{
        .uuid = *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000099"),
        .name = "prepared",
        .parent = {},
        .mesh_uri = gneiss::engine::resource_uri(before.mesh),
        .material_uri = gneiss::engine::resource_uri(before.material),
    };
    gneiss::scene_node_id created{};
    check(gneiss::test_support::take_value(
              gneiss::engine::create_mesh_renderer_node(
                  *candidate->native_world(), candidate_assets, new_scene, creation, &cleared),
              created) == gneiss::engine::error::invalid_argument &&
          !created.is_valid());
    check(gneiss::test_support::take_value(
              gneiss::engine::create_mesh_renderer_node(
                  *candidate->native_world(), candidate_assets, new_scene, creation, &before),
              created) == gneiss::engine::error::success);
    const auto created_node = created;
    check(gneiss::test_support::take_value(
              gneiss::engine::create_mesh_renderer_node(
                  *candidate->native_world(), candidate_assets, new_scene, creation, &before),
              created) == gneiss::engine::error::invalid_argument &&
          !created.is_valid());
    check(candidate->scenes()->destroy_node(new_scene, gneiss::scene_node_id{created_node}) ==
          gneiss::engine::error::success);
    check(files->reads == binding_reads);
  }
  check(candidate->native_world()->entity_remove_mesh_renderer(
            gneiss::entity_id{ordinary.node.get()}) == gneiss::engine::error::success);
  const auto snapshot_reads = files->reads;
  check(files->reads == snapshot_reads);
  active.swap(candidate);
  std::uint64_t count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active->native_world()->entity_count(), count)} == gneiss::engine::error::success &&
        count == 2U);
  check(gneiss::test_support::take_value(active->scenes()->get_node_count(old_scene), count) ==
        gneiss::engine::error::invalid_handle);
  check(gneiss::test_support::take_value(active->scenes()->get_node_count(new_scene), count) ==
            gneiss::engine::error::success &&
        count == 2U);
  old_material = {};
  candidate.reset();
  // 旧域已析构；此前借用的 World 指针不再可访问。
  check(old_pixels->parameters.color[0] == 1.0F);

  // 构造失败时仅回收候选节点；非法引用不能退回主线程文件读取。
  check(gneiss::test_support::prepare_scene_for_test(active_assets, executor, false,
                                                     "asset://s.scene", prepared, scene_error,
                                                     {}) == gneiss::engine::error::success);
  candidate = std::make_unique<runtime::scene_domain>();
  check(candidate->initialize() == gneiss::engine::error::success);
  // 空绑定表可预留尚未加载的资源身份；使用非法 URI 验证失败回收。
  bool invalid_dependency = false;
  for (auto& description : prepared.description.objects) {
    if (auto* renderer = description.components.get<gneiss::engine::mesh_renderer_description>()) {
      auto missing = *renderer;
      missing.mesh_uri = "invalid-uri";
      description.components.set(std::move(missing));
      invalid_dependency = true;
      break;
    }
  }
  check(invalid_dependency);
  const auto failure_reads = files->reads;
  gneiss::scene_instance_id failed_instance{};
  check(gneiss::test_support::take_value(gneiss::test_support::scene_fixture::instantiate_scene(
                                             *candidate->native_world(), std::move(prepared), {}),
                                         failed_instance) ==
        gneiss::engine::error::invalid_argument);
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
