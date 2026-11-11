// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_render.hpp"
#include "../support/async_scene.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/mesh_objects.hpp"
#include "../support/prepare_render.hpp"
#include "../support/scene_access.hpp"
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
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/scene/scene_update.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/scene.hpp"
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
  auto active_result = engine::create_native();
  check(bool(active_result));
  auto active = std::move(*active_result);
  gneiss::scene_instance_id old_scene{};
  engine::scene_content initial;
  engine::ref<const engine::scene> initial_asset;
  engine::scene_diagnostic initial_error;
  check(test_support::load_scene_content(active_assets, executor, "asset://s.scene", initial,
                                         initial_error, &initial_asset)
            .ok());
  auto initial_composed = test_support::compose(initial);
  check(bool(initial_composed));
  auto initial_dependencies = engine::scene_dependencies(*initial_composed);
  check(bool(initial_dependencies));
  engine::scene_resource_bindings initial_bindings;
  for (const auto& request : (*initial_dependencies)) {
    engine::render_resource_ref reference;
    engine::asset_diagnostic error;
    check(gneiss::test_support::load_render(active_render, request.type, request.uri, reference,
                                            error)
              .ok());
    initial_bindings.emplace(std::string(engine::resource_uri(reference)), reference);
  }
  // 通用应用只消费显式引用；缺失绑定不得隐式创建资产请求或访问文件。
  {
    auto empty =
        gneiss::test_support::scene_access{*active}->create_empty(engine::guid::generate());
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
    auto applied = engine::apply_scene(*active.get(), *empty, associations, {}, target, {});
    check(!applied && applied.error() == engine::error::not_ready);
    check(active_assets.cached_count() == cached && files->reads == before_reads);
    check(gneiss::test_support::scene_access{*active}->unload(*empty).ok());
  }
  const auto reads_before_instantiation = files->reads;
  auto direct = gneiss::test_support::scene_fixture::instantiate_scene(*active.get(), active_assets,
                                                                       initial_asset);
  check(bool(direct) && direct->node_count == initial_composed->objects.size());
  check(direct->assets.size() == (*initial_dependencies).size());
  check(files->reads == reads_before_instantiation);
  old_scene = direct->instance;
  auto second_instance = gneiss::test_support::scene_fixture::instantiate_scene(
      *active.get(), active_assets, initial_asset);
  check(bool(second_instance) && second_instance->instance != old_scene);
  check(gneiss::test_support::scene_access{*active}->unload(second_instance->instance).ok());
  check(gneiss::test_support::scene_access{*active}->get_node_count(old_scene).ok());
  check(gneiss::test_support::scene_fixture::instantiate_scene(*active.get(), active_assets, {})
            .error()
            .error == engine::error::invalid_argument);
  auto baseline = gneiss::test_support::compose(initial);
  check(bool(baseline));
  auto mapping = gneiss::test_support::associations(direct->objects);
  auto scene_reloaded = engine::apply_scene(*active.get(), old_scene, mapping, *baseline, *baseline,
                                            initial_bindings);
  check(scene_reloaded && *scene_reloaded);
  check(files->reads == reads_before_instantiation);
  auto invalid_apply =
      engine::apply_scene(*active.get(), {}, mapping, *baseline, *baseline, initial_bindings);
  check(!invalid_apply && invalid_apply.error() == engine::error::invalid_handle);
  engine::error foreign_reload;
  std::thread foreign_thread([&] {
    auto result = engine::apply_scene(*active.get(), old_scene, mapping, *baseline, *baseline,
                                      initial_bindings);
    foreign_reload = result ? engine::error::success : result.error();
  });
  foreign_thread.join();
  check(foreign_reload == engine::error::invalid_state);
  initial_bindings.clear();
  gneiss::test_support::scene_node_info old_info;
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*active}->get_node_info(old_scene, 0U), old_info) ==
        gneiss::engine::error::success);
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
  gneiss::engine::scene_content prepared;
  gneiss::engine::scene_diagnostic scene_error;
  check(gneiss::test_support::load_scene_content(active_assets, executor, "asset://s.scene",
                                                 prepared, scene_error,
                                                 {}) == gneiss::engine::error::success);
  auto candidate_result = engine::create_native();
  check(bool(candidate_result));
  auto candidate = std::move(*candidate_result);
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
  auto expected_prefab_asset = prepared.prefabs.begin()->second.resource;
  check(bool(expected_prefab_asset));
  // 缺少来源引用必须拒绝，不能通过临时描述回写缓存。
  auto missing_source = prepared;
  missing_source.prefabs.begin()->second.description.reset();
  gneiss::scene_instance_id rejected_scene{};
  check(gneiss::test_support::take_value(gneiss::test_support::scene_fixture::instantiate_scene(
                                             *candidate.get(), std::move(missing_source), bindings),
                                         rejected_scene) == gneiss::engine::error::not_found &&
        rejected_scene == gneiss::scene_instance_id{});
  auto created = gneiss::test_support::scene_fixture::instantiate_scene(
      *candidate.get(), std::move(prepared), bindings);
  check(bool(created));
  new_scene = created->instance;
  check(new_scene != gneiss::scene_instance_id{});
  // 投影重建使用通用应用：即使输入包含 Prefab，运行节点也不持有来源绑定。
  {
    auto empty =
        gneiss::test_support::scene_access{*candidate}->create_empty(engine::guid::generate());
    check(bool(empty));
    auto rebuilt = gneiss::test_support::scene_fixture::instantiate_scene(
        *candidate.get(), instantiation_input, bindings);
    check(bool(rebuilt));
    check(gneiss::test_support::scene_access{*candidate}->unload(*empty).ok());
    check(bool(rebuilt) && rebuilt->instance != *empty &&
          rebuilt->node_count == created->node_count);
    bool source_address = false;
    for (const auto& object : rebuilt->objects)
      source_address = source_address || bool(object.source);
    check(source_address);
    for (std::size_t index = 0; index < rebuilt->node_count; ++index) {
      auto info =
          gneiss::test_support::scene_access{*candidate}->get_node_info(rebuilt->instance, index);
      check(bool(info));
    }
    check(gneiss::test_support::scene_access{*candidate}->unload(rebuilt->instance).ok());
    check(files->reads == reads);
  }
  std::uint64_t unchanged_count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active.get()->entity_count(), unchanged_count)} == gneiss::engine::error::success &&
        unchanged_count == 1U);
  check(resources.get_material(gneiss::engine::resource_rid(old_material))->parameters.color[0] ==
        1.0F);
  check(files->reads == reads);
  // 同步创建不读文件；失败清理新建成员，不影响已有场景。
  {
    const auto baseline_count = *candidate.get()->entity_count();
    engine::error foreign;
    std::thread worker([&] {
      auto result = gneiss::test_support::scene_fixture::instantiate_scene(
          *candidate.get(), instantiation_input, bindings);
      foreign = result ? engine::error::success : result.error();
    });
    worker.join();
    check(foreign == engine::error::invalid_state);
    auto broken = instantiation_input;
    auto broken_description = engine::make_ref<engine::scene_description>(*broken.description);
    broken_description->objects.back().transform.scale = {0, 1, 1};
    broken.description = std::move(broken_description);
    check(!gneiss::test_support::scene_fixture::instantiate_scene(*candidate.get(),
                                                                  std::move(broken), bindings));
    check(*candidate.get()->entity_count() == baseline_count);
    check(files->reads == reads);
  }
  const auto selected_material = gneiss::engine::resource_rid(material);
  // 多个场景共享相同 URI 的资源身份。
  gneiss::scene_instance_id additional{};
  prepared = instantiation_input;
  check(gneiss::test_support::take_value(gneiss::test_support::scene_fixture::instantiate_scene(
                                             *candidate.get(), std::move(prepared), bindings),
                                         additional)
            .ok());
  gneiss::scene_instance_id duplicate_scene{};
  prepared = instantiation_input;
  check(gneiss::test_support::take_value(gneiss::test_support::scene_fixture::instantiate_scene(
                                             *candidate.get(), std::move(prepared), bindings),
                                         duplicate_scene)
            .ok());
  check(gneiss::test_support::scene_access{*candidate}->unload(duplicate_scene) ==
        gneiss::engine::error::success);
  check(gneiss::test_support::scene_access{*candidate}->unload(additional) ==
        gneiss::engine::error::success);
  material = {};
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr &&
        resources.get_material(selected_material)->parameters.color[1] == 1.0F);
  gneiss::engine::render_resource_ref next_version;
  check(gneiss::test_support::take_value(candidate_render.find(requests.back()), next_version) ==
        gneiss::engine::error::success);
  gneiss::test_support::scene_node_info ordinary;
  gneiss::test_support::scene_node_info prefab_node;
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*candidate}->get_node_info(new_scene, 0U),
            ordinary) == gneiss::engine::error::success);
  const auto prefab_member = std::ranges::find_if(
      created->objects, [](const auto& object) { return bool(object.source); });
  check(prefab_member != created->objects.end());
  bool found_prefab_member = false;
  for (std::size_t index = 0; index < created->node_count; ++index) {
    auto info = gneiss::test_support::scene_access{*candidate}->get_node_info(new_scene, index);
    check(bool(info));
    if (info->entity == prefab_member->entity) {
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
  auto matches = [&](const gneiss::engine::mesh_renderer& expected_value) {
    auto current =
        candidate->find(ordinary.entity).try_component<gneiss::engine::mesh_renderer_component>();
    return current && current->value.mesh == expected_value.mesh &&
           current->value.material == expected_value.material;
  };
  const auto edit_reads = files->reads;
  check(gneiss::test_support::assign_mesh(*candidate.get(), candidate_assets, new_scene,
                                          ordinary.node, "asset://m.mesh",
                                          "asset://m.material") == gneiss::engine::error::success);
  check(files->reads == edit_reads);
  check(matches(expected));
  check(gneiss::test_support::assign_mesh(
            *candidate.get(), candidate_assets, new_scene, ordinary.node, "asset://m.mesh",
            "asset://missing.material") == gneiss::engine::error::not_found);
  check(matches(expected));
  files->files["other.material"] = files->files["m.material"];
  gneiss::engine::render_resource_ref edited_material;
  check(gneiss::test_support::load_render(candidate_render,
                                          gneiss::engine::render_resource_kind::material,
                                          "asset://other.material", edited_material,
                                          diagnostic) == gneiss::engine::error::success);
  check(gneiss::test_support::assign_mesh(
            *candidate.get(), candidate_assets, new_scene, ordinary.node, "asset://m.mesh",
            "asset://other.material") == gneiss::engine::error::success);
  const gneiss::engine::mesh_renderer edited{
      .mesh = current_mesh, .material = edited_material.as<gneiss::engine::material>()};
  check(matches(edited));
  check(resources.get_material(gneiss::engine::resource_rid(old_material))->parameters.color[0] ==
        1.0F);
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr);
  candidate_render.collect_unused();
  check(resources.get_material(selected_material) != nullptr);
  {
    auto object = candidate->find(ordinary.entity);
    auto* borrowed = object.try_component<gneiss::engine::mesh_renderer_component>();
    check(borrowed != nullptr);
    const auto before = *borrowed;
    const auto binding_reads = files->reads;
    auto cleared = before;
    cleared.value.material = {};
    *borrowed = cleared;
    auto* empty = object.try_component<gneiss::engine::mesh_renderer_component>();
    check(empty && empty->value.mesh == before.value.mesh && !empty->value.material);
    gneiss::engine::error worker_result;
    std::thread worker([&] {
      worker_result = object.add_component<gneiss::engine::mesh_renderer_component>(before);
    });
    worker.join();
    check(worker_result == gneiss::engine::error::invalid_state);
    *object.try_component<gneiss::engine::mesh_renderer_component>() = before;
    check(files->reads == binding_reads);
  }
  check(candidate.get()
            ->find(gneiss::entity_id{ordinary.node.get()})
            .remove_component<gneiss::engine::mesh_renderer_component>() ==
        gneiss::engine::error::success);
  const auto snapshot_reads = files->reads;
  check(files->reads == snapshot_reads);
  active.swap(candidate);
  std::uint64_t count{};
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active.get()->entity_count(), count)} == gneiss::engine::error::success &&
        count == 2U);
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*active}->get_node_count(old_scene), count) ==
        gneiss::engine::error::invalid_handle);
  check(gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*active}->get_node_count(new_scene), count) ==
            gneiss::engine::error::success &&
        count == 2U);
  old_material = {};
  candidate.reset();
  // 旧域已析构；此前借用的 World 指针不再可访问。
  check(old_pixels->parameters.color[0] == 1.0F);

  // 构造失败时仅回收候选节点；非法引用不能退回主线程文件读取。
  check(gneiss::test_support::load_scene_content(active_assets, executor, "asset://s.scene",
                                                 prepared, scene_error,
                                                 {}) == gneiss::engine::error::success);
  candidate_result = engine::create_native();
  check(bool(candidate_result));
  candidate = std::move(*candidate_result);
  // 空绑定表可预留尚未加载的资源身份；使用非法 URI 验证失败回收。
  bool invalid_dependency = false;
  auto invalid_description = engine::make_ref<engine::scene_description>(*prepared.description);
  prepared.description = invalid_description;
  for (auto& description : invalid_description->objects) {
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
                                             *candidate.get(), std::move(prepared), {}),
                                         failed_instance) ==
        gneiss::engine::error::invalid_argument);
  check(failed_instance == gneiss::scene_instance_id{});
  check(files->reads == failure_reads);
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            candidate.get()->entity_count(), count)} == gneiss::engine::error::success &&
        count == 0U);
  check(gneiss::engine::error{gneiss::test_support::assign_value(
            active.get()->entity_count(), count)} == gneiss::engine::error::success &&
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
