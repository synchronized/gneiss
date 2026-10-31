// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/movement_component.hpp"
#include "../support/value_result.hpp"
#include "commands/author_property_edit.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "runtime/application.hpp"
#include <cstdio>
#include <source_location>
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
using namespace gneiss::test_support;
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok)
    throw std::runtime_error("Prefab 组件覆盖测试失败，行=" + std::to_string(at.line()));
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  check(e::register_scene_component(operations()).ok());
  constexpr auto type = movement_description::type_guid;
  constexpr auto root_id = e::guid::from_bytes({87, 2});
  constexpr auto child_id = e::guid::from_bytes({87, 3});
  auto source = std::make_shared<e::prefab_description>();
  source->uuid = e::guid::from_bytes({87, 1});
  source->root_uuid = root_id;
  e::object_description root;
  root.uuid = root_id;
  source->objects.push_back(root);
  auto child = root;
  child.uuid = child_id;
  child.parent_uuid = root_id;
  child.components.set(movement_description{});
  source->objects.push_back(child);
  e::prefab_reference_description reference{.uri = "asset://movement.prefab", .overrides = {}};
  e::prefab_description effective;
  e::scene_diagnostic diagnostic;
  reference.component_overrides.push_back({root_id, type, movement_description{.speed = 4}});
  check(e::prepare_prefab_reference(*source, reference, effective, &diagnostic).ok());
  check(effective.objects[0].components.get<movement_description>()->speed == 4);
  auto collision = *source;
  collision.objects[0].components.set(movement_description{});
  check(e::prepare_prefab_reference(collision, reference, effective, &diagnostic) ==
        e::error::invalid_state);
  check(diagnostic.path.find(root_id.string()) != std::string::npos &&
        diagnostic.path.find(type.string()) != std::string::npos);
  check(effective.objects[0].components.get<movement_description>()->speed == 4);
  reference.component_overrides.clear();
  reference.component_overrides.push_back({child_id, type, {}});
  check(e::prepare_prefab_reference(*source, reference, effective).ok() &&
        effective.objects[1].components.empty());
  auto deleted = *source;
  deleted.objects[1].components.erase(type);
  check(e::prepare_prefab_reference(deleted, reference, effective).ok());
  check(e::prepare_prefab_reference(*source, reference, effective).ok() &&
        effective.objects[1].components.empty());
  deleted.objects.pop_back();
  check(e::prepare_prefab_reference(deleted, reference, effective, &diagnostic) ==
        e::error::not_found);
  reference.overrides.push_back({.key = {.source_node_uuid = child_id,
                                         .type_guid = type,
                                         .property_id = e::meta::property_id{1}},
                                 .value = e::value{6.0F}});
  check(e::prepare_prefab_reference(*source, reference, effective) == e::error::invalid_argument);
  reference.component_overrides.clear();
  check(e::prepare_prefab_reference(*source, reference, effective).ok());
  check(effective.objects[1].components.get<movement_description>()->speed == 6);
  deleted = *source;
  deleted.objects[1].components.erase(type);
  check(e::prepare_prefab_reference(deleted, reference, effective, &diagnostic) ==
        e::error::not_found);
  reference.overrides[0].key.property_id = e::meta::property_id{99};
  check(e::prepare_prefab_reference(*source, reference, effective, &diagnostic).failed());
  check(diagnostic.path.find("99") != std::string::npos);
  reference.overrides[0].key.property_id = e::meta::property_id{1};
  reference.overrides[0].value = e::value{std::string("wrong")};
  check(e::prepare_prefab_reference(*source, reference, effective).failed());
  reference.overrides.clear();
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::runtime::application::create({}, app).ok());
  auto& world = *app->native_world();
  ed::scene_document document;
  check(document.create_empty(*app, &world).ok());
  e::object_description instance;
  instance.uuid = e::guid::from_bytes({87, 4});
  instance.prefab = reference;
  std::array<e::scene_mesh_binding, 2> bindings{};
  gneiss::scene_node_id root_node;
  check(gneiss::test_support::take_value(
            app->scenes()->instantiate_prefab_ready(document.scene_handle(), instance,
                                                    e::prefab_asset_ref{source}, bindings, false),
            root_node)
            .ok());
  check(document.synchronize_nodes().ok());
  auto* child_node = document.find_prefab_source(instance.uuid, child_id);
  check(child_node != nullptr);
  const auto node = child_node->node;
  ed::editor_command_history history;
  ed::component_edit_target target{
      .instance_uuid = instance.uuid, .source_uuid = root_id, .component_type = type};
  auto action = [&](ed::prefab_component_action value) {
    return ed::edit_prefab_component_structure(document, history, target, value);
  };
  check(action(ed::prefab_component_action::add).ok());
  check(action(ed::prefab_component_action::add) == e::error::invalid_state);
  check(ed::edit_component_description(document, history, world, target,
                                       {.path = {e::meta::property_id{1}}, .data = e::value{5.0F}},
                                       0)
            .ok());
  auto state = document.capture_component_override(root_node, type);
  check(state && state->added->get<movement_description>()->speed == 5 &&
        state->properties.empty());
  check(action(ed::prefab_component_action::remove).ok());
  state = document.capture_component_override(root_node, type);
  check(state && !state->added && !state->removed && state->properties.empty());
  check(history.undo().ok());
  check(document.components(root_node)->get<movement_description>()->speed == 5);
  check(history.undo().ok());
  check(document.components(root_node)->get<movement_description>()->speed == 1);
  check(history.redo().ok());
  check(history.clear().ok());
  target.source_uuid = child_id;
  check(ed::edit_component_description(document, history, world, target,
                                       {.path = {e::meta::property_id{1}}, .data = e::value{7.0F}},
                                       0)
            .ok());
  check(action(ed::prefab_component_action::remove).ok());
  state = document.capture_component_override(node, type);
  check(state && state->removed && state->properties.empty());
  check(history.undo().ok() && document.components(node)->get<movement_description>()->speed == 7);
  check(history.redo().ok());
  check(action(ed::prefab_component_action::restore_source).ok());
  check(document.components(node)->get<movement_description>()->speed == 1);
  check(history.undo().ok() && !document.components(node)->find(type));
  // 保存的是增删覆盖，来源资源内容保持不变。
  e::scene_description saved;
  check(gneiss::test_support::assign_value(
            app->scenes()->capture_description(document.scene_handle()), saved)
            .ok());
  check(saved.objects[0].prefab->component_overrides.size() == 2);
  auto bytes = e::encode_scene(saved);
  check(bytes.has_value());
  auto decoded = e::decode_scene(*bytes);
  check(decoded.has_value());
  check(decoded->objects[0].prefab->component_overrides.size() == 2);
  auto contradictory = saved;
  contradictory.objects[0].prefab->overrides.push_back(
      {.key = {.source_node_uuid = root_id,
               .type_guid = type,
               .property_id = e::meta::property_id{1}},
       .value = e::value{2.0F}});
  check(!e::encode_scene(contradictory));
  e::scene_preparation_inputs inputs;
  inputs.scene = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(*decoded)};
  inputs.prefabs.emplace(reference.uri, e::prefab_asset_ref{source});
  e::scene_instantiation_data prepared;
  check(e::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {}).ok());
  gneiss::scene_instance_id loaded;
  check(gneiss::test_support::take_value(
            app->scenes()->instantiate_prepared(std::move(prepared), {}), loaded)
            .ok());
  gneiss::scene_node_id loaded_root;
  check(
      gneiss::test_support::take_value(app->scenes()->find_node(loaded, instance.uuid), loaded_root)
          .ok());
  check(e::capture_components(world, loaded_root)->get<movement_description>()->speed == 5);
  check(app->scenes()->unload(loaded).ok());
  // 所有实例预检查后才提交；冲突保持节点身份、旧组件和覆盖不变。
  auto updated = std::make_shared<e::prefab_description>(collision);
  check(app->scenes()->reload_prefab_loaded(document.scene_handle(), reference.uri,
                                            e::prefab_asset_ref{updated},
                                            &diagnostic) == e::error::invalid_state);
  check(!diagnostic.path.empty());
  check(document.components(root_node)->get<movement_description>()->speed == 5);
  check(!document.components(node)->find(type));
  // 来源对象改名、改默认值不会改变移除覆盖；组件消失后又出现仍隐藏。
  updated = std::make_shared<e::prefab_description>(*source);
  updated->objects[1].name = "renamed";
  updated->objects[1].components.erase(type);
  check(app->scenes()
            ->reload_prefab_loaded(document.scene_handle(), reference.uri,
                                   e::prefab_asset_ref{updated})
            .ok());
  check(!document.components(node)->find(type));
  check(app->scenes()
            ->reload_prefab_loaded(document.scene_handle(), reference.uri,
                                   e::prefab_asset_ref{source})
            .ok());
  check(!document.components(node)->find(type));
  check(history.clear().ok());
  check(action(ed::prefab_component_action::restore_source).ok());
  check(document.components(node)->get<movement_description>()->speed == 1);
  check(source->objects[0].components.empty() &&
        source->objects[1].components.get<movement_description>()->speed == 1);
  return 0;
} catch (const std::exception& failure) {
  std::fprintf(stderr, "%s\n", failure.what());
  return 1;
}
