// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/movement_component.hpp"
#include "../support/value_result.hpp"
#include "commands/author_property_edit.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "runtime/application.hpp"
#include <cstdio>
#include <source_location>
#include <thread>
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
using namespace gneiss::test_support;
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok)
    throw std::runtime_error("组件生命周期测试失败，行=" + std::to_string(at.line()));
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  check(e::register_scene_component(operations()).ok());
  auto choices = e::component_editor_types();
  check(choices && choices->size() == 3);
  for (const auto& choice : *choices) {
    check(choice.removable);
    check(choice.addable == (choice.type != e::mesh_renderer_description::type_guid));
  }
  check(!e::default_component_description({}));
  check(e::default_component_description(e::mesh_renderer_description::type_guid).error() ==
        e::error::unsupported);
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::runtime::application::create({}, app).ok());
  auto& world = *app->native_world();
  ed::scene_document document;
  check(document.create_empty(*app, &world).ok());
  gneiss::scene_node_id node;
  check(document.create_node("movement", {}, node).ok());
  const auto uuid = document.selected_node()->uuid;
  constexpr auto type = movement_description::type_guid;
  ed::editor_command_history history;
  auto add = [&] { return ed::add_component_description(document, history, uuid, type); };
  auto remove = [&] { return ed::remove_component_description(document, history, uuid, type); };
  auto current = [&] { return state(world).get<movement_component>(entity(world, node)); };
  check(remove() == e::error::not_found && history.size() == 0);
  ed::editor_command_history disabled{0};
  check(ed::add_component_description(document, disabled, uuid, type).failed() && !current());
  check(add().ok() && current()->data.speed == 1);
  check(add() == e::error::invalid_state && history.size() == 1);
  check(ed::remove_component_description(document, disabled, uuid, type).failed() && current());
  check(ed::edit_component_description(
            document, history, world, {.object_uuid = uuid, .component_type = type},
            {.path = {e::meta::property_id{1}}, .data = e::value{4.0F}}, 0)
            .ok());
  std::weak_ptr<int> lifetime = current()->lifetime;
  check(remove().ok() && !current() && lifetime.expired() && history.size() == 3);
  check(history.undo().ok() && current()->data.speed == 4);
  check(history.redo().ok() && !current());
  check(history.undo().ok());
  check(history.undo().ok() && current()->data.speed == 1);
  check(history.undo().ok() && !current());
  check(history.redo().ok());
  check(history.redo().ok() && current()->data.speed == 4);
  // 历史不删除外部修改后的不同组件。
  check(document.set_component_description(node, movement_description{.speed = 5}).ok());
  check(history.redo() == e::error::invalid_state && current()->data.speed == 5);
  check(document.set_component_description(node, movement_description{.speed = 4}).ok());
  check(history.redo().ok() && !current());
  auto snapshot = [&] {
    e::scene_description result;
    check(gneiss::test_support::assign_value(
              app->scenes()->capture_description(document.scene_handle()), result)
              .ok());
    return result;
  };
  auto removed = snapshot();
  check(removed.objects.size() == 1 && removed.objects[0].components.empty());
  check(history.undo().ok() && current()->data.speed == 4);
  auto encoded = e::encode_scene(snapshot());
  check(encoded.has_value());
  auto decoded = e::decode_scene(*encoded);
  check(decoded.has_value());
  e::scene_preparation_inputs inputs;
  inputs.scene = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(std::move(*decoded))};
  e::scene_instantiation_data prepared;
  e::scene_diagnostic diagnostic;
  check(e::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {}).ok());
  gneiss::scene_instance_id loaded;
  check(gneiss::test_support::take_value(
            app->scenes()->instantiate_prepared(std::move(prepared), {}), loaded)
            .ok());
  gneiss::scene_node_id loaded_node;
  check(gneiss::test_support::take_value(app->scenes()->find_node(loaded, uuid), loaded_node).ok());
  check(state(world).get<movement_component>(entity(world, loaded_node))->data.speed == 4);
  check(app->scenes()->remove_component_description(document.scene_handle(), loaded_node, type) ==
        e::error::invalid_handle);
  check(app->scenes()->unload(loaded).ok());
  e::error cross_thread = e::error::success;
  std::thread worker([&] { cross_thread = e::remove_component({world, node}, type); });
  worker.join();
  check(cross_thread.failed() && current());
  check(history.clear().ok());
  check(document.remove_component_description(node, type).ok());
  check(document.add_component_description(node, movement_description{.speed = -1}) ==
            e::error::invalid_argument &&
        !current());
  check(document.add_component_description(node, movement_description{.speed = 13}) ==
            e::error::invalid_state &&
        !current());
  check(add().ok());
  // 相机使用同一默认工厂与历史，不再在面板手写快照。
  check(ed::add_component_description(document, history, uuid, e::camera_description::type_guid)
            .ok());
  check(ed::remove_component_description(document, history, uuid, e::camera_description::type_guid)
            .ok());
  check(history.undo().ok());
  check(document.components(node)->get<e::camera_description>() != nullptr);
  // Prefab 根和子节点的结构编辑现在产生显式覆盖。
  auto prefab = std::make_shared<e::prefab_description>();
  prefab->uuid = e::guid::from_bytes({86, 1});
  prefab->root_uuid = e::guid::from_bytes({86, 2});
  e::object_description root;
  root.uuid = prefab->root_uuid;
  root.components.set(movement_description{});
  prefab->objects.push_back(root);
  auto child = root;
  child.uuid = e::guid::from_bytes({86, 4});
  child.parent_uuid = root.uuid;
  prefab->objects.push_back(child);
  e::object_description reference;
  reference.uuid = e::guid::from_bytes({86, 3});
  reference.prefab.emplace();
  reference.prefab->uri = "asset://movement.prefab";
  const std::array<e::scene_mesh_binding, 2> bindings{};
  gneiss::scene_node_id prefab_node;
  check(gneiss::test_support::take_value(
            app->scenes()->instantiate_prefab_ready(document.scene_handle(), reference,
                                                    e::prefab_asset_ref{prefab}, bindings, false),
            prefab_node)
            .ok());
  check(document.validate_selection().ok());
  check(document.add_component_description(
            prefab_node, *e::default_component_description(e::camera_description::type_guid)) ==
        e::error::success);
  check(document.remove_component_description(prefab_node, type) == e::error::success);
  const auto child_node = document.find_prefab_source(reference.uuid, child.uuid);
  check(bool(child_node));
  const auto child_handle = child_node->node;
  check(document.remove_component_description(child_handle, type) == e::error::success);
  check(document.add_component_description(
            child_handle, *e::default_component_description(e::camera_description::type_guid)) ==
        e::error::success);
  check(history.clear().ok());
  lifetime = current()->lifetime;
  check(app->scenes()->unload(document.scene_handle()).ok() && lifetime.expired());
  check(history.clear().ok());
  return 0;
} catch (const std::exception& failure) {
  std::fprintf(stderr, "%s\n", failure.what());
  return 1;
}
