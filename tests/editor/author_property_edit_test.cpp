// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "commands/author_property_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include "engine/core/error.hpp"
#include "engine/core/meta/type.hpp"
#include "engine/core/rid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
int camera_edits(gneiss::runtime::application& app, gneiss::editor::scene_document& session,
                 gneiss::scene_node_id ordinary) {
  namespace e = gneiss::engine;
  namespace ed = gneiss::editor;
  using p = e::meta::property_id;
  auto& world = *app.native_world();
  const auto ordinary_uuid = session.selected_node()->uuid;
  if (session.set_camera(ordinary, {.settings = {1.0F, 0.1F, 100.0F}}).failed())
    return 31;
  ed::editor_command_history history;
  ed::component_edit_target target{.object_uuid = ordinary_uuid,
                                   .component_type = e::camera_description::type_guid};
  ed::property_edit edit{.path = {p{1}, p{2}}, .data = e::value{2.0F}};
  if (ed::edit_component_description(session, history, world, target, edit, 1).failed())
    return 32;
  auto read = [&](gneiss::entity_id entity) {
    e::math::camera_settings camera;
    if (world.entity_get_camera(entity, camera).failed())
      throw std::runtime_error("read camera");
    return camera;
  };
  auto ordinary_entity = session.find_node(ordinary_uuid)->entity;
  if (read(ordinary_entity).near != 2.0F || history.undo().failed() ||
      read(ordinary_entity).near != 0.1F || history.redo().failed())
    return 33;
  edit.data = e::value{200.0F};
  if (ed::edit_component_description(session, history, world, target, edit, 2) !=
          e::error::invalid_argument ||
      read(ordinary_entity).near != 2.0F || history.size() != 1)
    return 34;
  edit.path = {p{4}};
  edit.data = e::value{true};
  if (ed::edit_component_description(session, history, world, target, edit, 3) !=
      e::error::unsupported)
    return 35;
  // 整体替换允许 near/far 一起改变，不产生无效的中间相机。
  edit.path = {p{1}};
  edit.object_type = e::runtime_type_of<e::math::camera_settings>();
  edit.object =
      e::make_ref<e::math::camera_settings>(e::math::camera_settings{1.2F, 200.0F, 400.0F});
  if (ed::edit_component_description(session, history, world, target, edit, 4).failed() ||
      read(ordinary_entity).far != 400.0F)
    return 36;

  edit.object_type = e::runtime_type_of<e::camera_description>();
  edit.object = gneiss::engine::make_ref<e::camera_description>();
  if (ed::edit_component_description(session, history, world, target, edit, 4) !=
          e::error::invalid_argument ||
      read(ordinary_entity).far != 400.0F)
    return 49;
  gneiss::scene_node_id other_camera;
  if (session.create_node("active", {}, other_camera).failed() ||
      session.set_camera(other_camera, {.settings = {1.0F, 0.1F, 100.0F}, .is_primary = true})
          .failed())
    return 50;
  gneiss::entity_id active_before, active_after;
  if (world.get_active_camera(active_before).failed() || history.undo().failed() ||
      history.redo().failed() || world.get_active_camera(active_after).failed() ||
      active_before != active_after)
    return 51;
  auto source = std::make_shared<e::prefab_description>();
  source->source_schema_version = 1;
  if (ed::make_editor_uuid(source->uuid).failed() ||
      ed::make_editor_uuid(source->root_uuid).failed())
    return 37;
  e::object_description root;
  root.uuid = source->root_uuid;
  root.name = "camera";
  root.components.set(e::camera_description{.settings = {1.0F, 0.1F, 100.0F}, .is_primary = true});
  source->objects.push_back(root);
  e::object_description child;
  if (ed::make_editor_uuid(child.uuid).failed())
    return 52;
  child.name = "child";
  child.parent_uuid = source->root_uuid;
  child.transform.translation.x = 1;
  source->objects.push_back(child);
  e::object_description author;
  if (ed::make_editor_uuid(author.uuid).failed())
    return 38;
  author.name = "instance";
  author.prefab.emplace();
  author.prefab->uri = "asset://camera.prefab.json";
  gneiss::scene_node_id prefab_root;
  const std::array<e::scene_mesh_binding, 2> bindings{};
  if (gneiss::test_support::take_value(
          app.scenes()->instantiate_prefab_ready(session.scene_handle(), author,
                                                 e::prefab_asset_ref{source}, bindings, false),
          prefab_root)
          .failed() ||
      session.synchronize_nodes().failed())
    return 39;
  ed::component_edit_target prefab_target{.instance_uuid = author.uuid,
                                          .source_uuid = source->root_uuid,
                                          .component_type = e::camera_description::type_guid};
  const auto* prefab_node = session.find_prefab_source(author.uuid, source->root_uuid);
  if (!prefab_node)
    return 40;
  auto prefab_entity = prefab_node->entity;
  // 来源标记主相机，但运行时已切到别处；编辑 fov 不应生成 is_primary 覆盖。
  if (session.set_camera(other_camera, {.settings = {1.0F, 0.1F, 100.0F}, .is_primary = true})
          .failed())
    return 60;
  ed::editor_command_history prefab_history;
  edit = {.path = {p{1}, p{1}}, .data = e::value{1.3F}};
  if (ed::edit_component_description(session, prefab_history, world, prefab_target, edit, 5)
          .failed() ||
      read(prefab_entity).fov != 1.3F ||
      source->objects[0].components.get<e::camera_description>()->settings.fov != 1.0F)
    return 41;
  auto snapshot = [&]() {
    std::string json;
    if (gneiss::test_support::assign_value(app.scenes()->serialize(session.scene_handle()), json)
            .failed())
      throw std::runtime_error("serialize");
    auto decoded = e::meta::deserialize<e::scene_description>(
        std::as_bytes(std::span{json.data(), json.size()}));
    if (!decoded)
      throw std::runtime_error("parse");
    return std::move(*decoded);
  };
  auto overrides = [&](const e::scene_description& scene) -> std::size_t {
    for (const auto& object : scene.objects)
      if (object.uuid == author.uuid)
        return object.prefab->overrides.size();
    throw std::runtime_error("missing prefab");
  };
  if (overrides(snapshot()) != 1 || prefab_history.undo().failed() || overrides(snapshot()) != 0 ||
      prefab_history.redo().failed() || overrides(snapshot()) != 1)
    return 42;
  ed::component_edit_target root_transform{.instance_uuid = author.uuid,
                                           .source_uuid = source->root_uuid,
                                           .component_type = e::transform_component_type};
  auto child_transform = root_transform;
  child_transform.source_uuid = child.uuid;
  ed::editor_command_history root_history, child_history;
  ed::property_edit translation{.path = {e::transform_translation_field},
                                .data = e::value{e::math::vector3{5, 0, 0}}};
  if (ed::edit_component_description(session, root_history, world, root_transform, translation, 10)
          .failed() ||
      overrides(snapshot()) != 1 || root_history.undo().failed() || root_history.redo().failed())
    return 53;
  translation.data = e::value{e::math::vector3{3, 0, 0}};
  if (ed::edit_component_description(session, child_history, world, child_transform, translation,
                                     11)
          .failed())
    return 54;
  translation.data = e::value{e::math::vector3{6, 0, 0}};
  if (ed::edit_component_description(session, child_history, world, child_transform, translation,
                                     11)
          .failed() ||
      child_history.size() != 1 || overrides(snapshot()) != 2 ||
      session.select(ordinary).failed() || child_history.undo().failed() ||
      overrides(snapshot()) != 1 || child_history.redo().failed() ||
      source->objects[1].transform.translation.x != 1)
    return 55;
  ed::property_edit bad_scale{.path = {e::transform_scale_field},
                              .data = e::value{e::math::vector3{0, 1, 1}}};
  if (ed::edit_component_description(session, child_history, world, child_transform, bad_scale,
                                     12) != e::error::invalid_argument ||
      child_history.size() != 1 || overrides(snapshot()) != 2)
    return 56;
  ed::property_edit restore{.path = {},
                            .object_type = e::runtime_type_of<e::math::transform>(),
                            .object =
                                gneiss::engine::make_ref<e::math::transform>(child.transform)};
  if (ed::edit_component_description(session, child_history, world, child_transform, restore, 0)
          .failed() ||
      overrides(snapshot()) != 1 || child_history.undo().failed() || overrides(snapshot()) != 2)
    return 57;
  auto saved = snapshot();
  // 保存后的描述重新准备和实例化，普通组件与 Prefab 差异均必须恢复。
  e::scene_preparation_inputs inputs;
  inputs.scene =
      e::asset_ref<const e::scene_description>{std::make_shared<e::scene_description>(saved)};
  inputs.prefabs.emplace(author.prefab->uri, e::prefab_asset_ref{source});
  e::scene_instantiation_data prepared;
  e::scene_diagnostic diagnostic;
  if (e::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {}).failed())
    return 43;
  gneiss::scene_instance_id loaded;
  if (gneiss::test_support::take_value(app.scenes()->instantiate_prepared(std::move(prepared), {}),
                                       loaded)
          .failed())
    return 44;
  bool found_ordinary = false, found_prefab = false, found_child = false;
  std::uint64_t count{};
  if (gneiss::test_support::take_value(app.scenes()->get_node_count(loaded), count).failed())
    return 45;
  for (std::uint64_t i = 0; i < count; ++i) {
    e::scene_node_info node;
    if (gneiss::test_support::take_value(app.scenes()->get_node_info(loaded, i), node).failed())
      return 46;
    if (node.prefab && node.prefab->instance_uuid == author.uuid) {
      if (node.prefab->source_node_uuid == source->root_uuid)
        found_prefab = read(node.entity).fov == 1.3F && node.local_transform.translation.x == 5;
      else if (node.prefab->source_node_uuid == child.uuid)
        found_child = node.local_transform.translation.x == 6;
    } else if (node.uuid == ordinary_uuid) {
      const auto camera = read(node.entity);
      found_ordinary =
          camera.near == 200.0F && camera.far == 400.0F && node.local_transform.translation.x == 6;
    }
  }
  if (app.scenes()->unload(loaded).failed() || !found_ordinary || !found_prefab || !found_child)
    return 47;
  // 移除后不把旧历史应用到另一个节点。
  if (app.scenes()->remove_prefab_instance(session.scene_handle(), author.uuid).failed() ||
      session.synchronize_nodes().failed() || prefab_history.undo() != e::error::not_found)
    return 48;
  return 0;
}
}

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::engine::world* world_handle = nullptr;
  scene_document session;
  editor_command_history history;
  scene_node_id first;
  scene_node_id second;
  gneiss::runtime::application_configuration desc;
  if (gneiss::engine::error{gneiss::runtime::application::create(desc, app)}.failed() ||
      (world_handle = app->native_world()) == nullptr ||
      session.create_empty(*app, world_handle).failed() ||
      session.create_node("first", {}, first).failed() ||
      session.create_node("second", {}, second).failed() || session.select(first).failed()) {
    return 1;
  }
  const auto entity = session.selected_node()->entity;
  const auto is_x = [&](float expected) {
    gneiss::engine::math::transform local{};
    return app->native_world()->entity_get_local_transform(entity, local) ==
               gneiss::engine::error::success &&
           std::abs(local.translation[0] - expected) < 0.0001F;
  };
  const auto edit_x = [&](float next, editor_command_history& target, std::uint64_t serial) {
    component_edit_target identity{.object_uuid = session.selected_node()->uuid,
                                   .component_type = gneiss::engine::transform_component_type};
    property_edit edit{.path = {gneiss::engine::transform_translation_field},
                       .data = gneiss::engine::value{gneiss::engine::math::vector3{next, 0, 0}}};
    return edit_component_description(session, target, *world_handle, identity, edit, serial);
  };
  // 同一次拖拽合并，新的交互形成独立历史；临时输入不被命令借用。
  if (edit_x(2.0F, history, 1U).failed() || edit_x(4.0F, history, 1U).failed() ||
      history.size() != 1U || !is_x(4.0F) || edit_x(6.0F, history, 2U).failed() ||
      history.size() != 2U || !is_x(6.0F)) {
    return 3;
  }
  // 选择改变后，撤销/重做必须仍定位原作者 UUID。
  if (session.select(second).failed() || history.undo().failed() || !is_x(4.0F) ||
      history.undo().failed() || !is_x(0.0F) || history.redo().failed() || !is_x(4.0F) ||
      history.redo().failed() || !is_x(6.0F)) {
    return 4;
  }
  if (session.select(first).failed()) {
    return 5;
  }
  editor_command_history no_capacity{0U};
  if (edit_x(8.0F, no_capacity, 3U) != gneiss::engine::error::invalid_argument || !is_x(6.0F) ||
      no_capacity.can_undo()) {
    return 6;
  }
  component_edit_target identity{.object_uuid = session.selected_node()->uuid,
                                 .component_type = gneiss::engine::transform_component_type};
  property_edit invalid{.path = {gneiss::engine::transform_scale_field},
                        .data = gneiss::engine::value{gneiss::engine::math::vector3{0, 1, 1}}};
  if (edit_component_description(session, history, *world_handle, identity, invalid, 4) !=
          gneiss::engine::error::invalid_argument ||
      history.size() != 2U) {
    return 8;
  }
  if (const auto status = camera_edits(*app, session, first); status != 0)
    return status;
  scene_subtree_snapshot removed;
  if (session.destroy_subtree(first, removed).failed() ||
      history.undo() != gneiss::engine::error::not_found || !history.can_undo()) {
    return 9;
  }
  history.clear();
  session.close();
  return 0;
} catch (...) {
  return 10;
}
