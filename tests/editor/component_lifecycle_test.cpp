// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/document_prefab.hpp"
#include "../support/movement_editor.hpp"
#include "../support/scene_instantiation.hpp"
#include "../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include "documents/commands/author_property_edit.hpp"
#include "documents/components/component_registration.hpp"
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/composition.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <algorithm>
#include <cstdio>
#include <source_location>
#include <thread>
namespace e = gneiss;
namespace ed = gneiss::editor;
using namespace gneiss::test_support;
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok)
    throw std::runtime_error("组件生命周期测试失败，行=" + std::to_string(at.line()));
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  auto ops = operations();
  ops.update = [](e::component_context& context,
                  const movement_description& description) -> e::error {
    if (description.speed == 13)
      return e::error::invalid_state;
    auto* component =
        state(*context.target.owner()).get<movement_component>(context.target.entity().get());
    if (!component)
      return e::error::not_found;
    component->data = description;
    return e::error::success;
  };
  check(e::register_scene_component(ops).ok());
  check(gneiss::editor::register_component_editor(movement_editor()).ok());
  check(ed::register_component_editor(movement_editor()) == e::error::invalid_argument);
  auto choices = gneiss::editor::component_editor_types();
  check(choices && choices->size() == 3);
  check(ed::register_component_editor({.type = e::guid::generate()}) == e::error::invalid_state);
  check(ed::component_editor(e::mesh_component_description::type_guid)->route ==
        ed::component_editor_route::mesh_assets);
  check(ed::component_editor(movement_description::type_guid)->route ==
        ed::component_editor_route::reflected);
  for (const auto& choice : *choices) {
    check(choice.removable);
    check(choice.addable == (choice.type != e::mesh_component_description::type_guid));
  }
  check(!gneiss::editor::default_component_description({}));
  check(gneiss::editor::default_component_description(e::mesh_component_description::type_guid)
            .error() == e::error::unsupported);
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create({}), app).ok());
  auto& world = *app->native_world();
  ed::scene_edit_session document;
  check(document.create_empty(*app, &world).ok());
  gneiss::entity_id node;
  check(document.create_node("movement", {}, node).ok());
  const auto uuid = document.selected_node()->uuid;
  constexpr auto type = movement_description::type_guid;
  ed::editor_command_history history;
  auto add = [&] { return ed::add_component_description(document, history, uuid, type); };
  auto remove = [&] { return ed::remove_component_description(document, history, uuid, type); };
  auto current = [&] {
    node = document.find_node(uuid)->node;
    return state(world).get<movement_component>(entity(world, node));
  };
  check(remove() == e::error::not_found && history.size() == 0);
  ed::editor_command_history disabled{0};
  check(ed::add_component_description(document, disabled, uuid, type).failed() && !current());
  check(add().ok() && current()->data.speed == 1);
  const auto identity = document.components(node)->identity(type);
  const auto check_identity = [&] {
    node = document.find_node(uuid)->node;
    check(document.components(node)->identity(type) == identity);
    check(e::capture_components(world, node)->identity(type) == identity);
  };
  check_identity();
  check(add() == e::error::invalid_state && history.size() == 1);
  check(ed::remove_component_description(document, disabled, uuid, type).failed() && current());
  std::weak_ptr<int> retained_runtime = current()->lifetime;
  check(ed::edit_component_description(document, history, world,
                                       {.object_uuid = uuid, .component_type = type},
                                       {.path = {e::property_id{1}}, .data = e::value{4.0F}}, 0)
            .ok());
  check(retained_runtime.lock() == current()->lifetime);
  check(history.undo().ok() && current()->data.speed == 1 &&
        retained_runtime.lock() == current()->lifetime);
  check(history.redo().ok() && current()->data.speed == 4 &&
        retained_runtime.lock() == current()->lifetime);
  std::weak_ptr<int> lifetime = current()->lifetime;
  check(remove().ok() && !current() && lifetime.expired() && history.size() == 3);
  check(history.undo().ok() && current()->data.speed == 4);
  check_identity();
  check(history.redo().ok() && !current());
  check(history.undo().ok());
  check(history.undo().ok() && current()->data.speed == 1);
  check(history.undo().ok() && !current());
  check(history.redo().ok());
  check(history.redo().ok() && current()->data.speed == 4);
  check_identity();
  auto before_independent = document.current_version();
  // 历史不删除外部修改后的不同组件。
  check(document
            .set_component_description(node, e::make_component_description(movement_description{5}))
            .ok());
  check(history.redo() == e::error::invalid_state && current()->data.speed == 5);
  check(document.restore_version(document.current_version(), before_independent).ok());
  check(history.redo().ok() && !current());
  auto snapshot = [&] {
    auto content = document.capture_content();
    check(content.has_value());
    auto description = content->description();
    check(description.has_value());
    return std::move(*description);
  };
  auto removed = snapshot();
  check(removed.objects.size() == 1 && removed.objects[0].components.empty());
  check(history.undo().ok() && current()->data.speed == 4);
  check_identity();
  auto encoded = gneiss::asset_file::serialize_bytes(snapshot());
  check(encoded.has_value());
  auto decoded = gneiss::asset_file::deserialize<e::scene_description>(*encoded);
  check(decoded.has_value());
  e::scene_content prepared;
  prepared.description = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(std::move(*decoded))};
  auto receipt =
      gneiss::test_support::scene_fixture::instantiate_scene(*app->native_world(), prepared, {});
  check(bool(receipt));
  const auto loaded = receipt->instance;
  const auto binding = std::ranges::find_if(receipt->objects, [&](const auto& entry) {
    return entry.object == uuid && !entry.source;
  });
  check(binding != receipt->objects.end());
  const auto loaded_object = world.object(binding->entity);
  check(loaded_object && loaded.contains(*loaded_object));
  const auto* loaded_component =
      state(world).get<movement_component>(entity(world, binding->entity));
  check(loaded_component && loaded_component->data.speed == 4);
  check(!document.scene_handle().contains(*loaded_object));
  check(e::remove_component({*loaded_object}, type).ok());
  loaded.unload();
  check(!loaded.is_valid() && !loaded_object->is_valid());
  e::error cross_thread = e::error::success;
  auto borrowed_target = world.object(gneiss::entity_id{node.get()});
  check(borrowed_target.has_value());
  std::thread worker([&] { cross_thread = e::remove_component({*borrowed_target}, type); });
  worker.join();
  check(cross_thread == e::error::invalid_state && current());
  check(history.clear().ok());
  check(document.remove_component_description(node, type).ok());
  check(document.add_component_description(node, e::make_component_description(movement_description{
                                                     -1})) == e::error::invalid_argument &&
        !current());
  check(
      document
          .add_component_description(node, e::make_component_description(movement_description{13}))
          .ok() &&
      !current());
  check(document.projection_status() == e::error::invalid_state);
  check(document.components(node)->get<movement_description>()->speed == 13);
  auto failed_content = document.serialize();
  check(failed_content.ok());
  const auto previous_scene = document.scene_handle();
  check(document.synchronize() == e::error::invalid_state);
  check(document.scene_handle() == previous_scene);
  check(previous_scene.is_valid());
  auto after_failure = document.serialize();
  check(after_failure && *after_failure == *failed_content);
  check(document
            .set_component_description(node, e::make_component_description(movement_description{4}))
            .ok());
  // 新编辑统一进入投影；未知基线完整同步成功后自动恢复健康状态。
  const auto previous_node = node;
  node = document.find_node(uuid)->node;
  check(document.scene_handle() != previous_scene);
  check(!previous_scene.is_valid());
  check(node != previous_node && document.projection_status().ok() && current()->data.speed == 4);
  check(document.remove_component_description(node, type).ok());
  check(add().ok());
  // 相机使用同一默认工厂与历史，不再在面板手写快照。
  check(ed::add_component_description(document, history, uuid, e::camera_description::type_guid)
            .ok());
  check(ed::remove_component_description(document, history, uuid, e::camera_description::type_guid)
            .ok());
  check(history.undo().ok());
  node = document.find_node(uuid)->node;
  check(document.components(node)->get<e::camera_description>() != nullptr);
  // Prefab 根和子节点的结构编辑现在产生显式覆盖。
  auto prefab = std::make_shared<e::prefab_description>();
  prefab->uuid = e::guid::from_bytes({86, 1});
  prefab->root_uuid = e::guid::from_bytes({86, 2});
  e::game_object_description root;
  root.uuid = prefab->root_uuid;
  root.components.set(movement_description{});
  prefab->objects.push_back(root);
  auto child = root;
  child.uuid = e::guid::from_bytes({86, 4});
  child.parent.object = root.uuid;
  prefab->objects.push_back(child);
  e::game_object_description reference;
  reference.uuid = e::guid::from_bytes({86, 3});
  reference.prefab.emplace();
  reference.prefab->uri = "asset://movement.prefab";
  const std::array<gneiss::editor::scene_mesh_binding, 2> bindings{};
  gneiss::entity_id prefab_node;
  check(gneiss::test_support::take_value(
            gneiss::test_support::place_document_prefab(
                document, reference, e::prefab_asset_ref{prefab}, bindings, false),
            prefab_node)
            .ok());
  check(document.validate_selection().ok());
  check(document.add_component_description(
            prefab_node, *gneiss::editor::default_component_description(
                             e::camera_description::type_guid)) == e::error::success);
  check(document.remove_component_description(prefab_node, type) == e::error::success);
  const auto child_node = document.find_prefab_source(reference.uuid, child.uuid);
  check(bool(child_node));
  const auto child_handle = child_node->node;
  check(document.remove_component_description(child_handle, type) == e::error::success);
  check(document.add_component_description(
            child_handle, *gneiss::editor::default_component_description(
                              e::camera_description::type_guid)) == e::error::success);
  check(history.clear().ok());
  lifetime = current()->lifetime;
  document.scene_handle().unload();
  check(!document.scene_handle().is_valid() && lifetime.expired());
  check(history.clear().ok());
  return 0;
} catch (const std::exception& failure) {
  std::fprintf(stderr, "%s\n", failure.what());
  return 1;
}
