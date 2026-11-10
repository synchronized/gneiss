// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/document_prefab.hpp"
#include "../support/movement_component.hpp"
#include "../support/scene_access.hpp"
#include "../support/scene_instantiation.hpp"
#include "../support/value_result.hpp"
#include "commands/author_property_edit.hpp"
#include "documents/scene_document_edit.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/scene/scene_validation.hpp"
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
  {
    e::prefab_component_override added{
        root_id, type, e::make_component_description(movement_description{4}), e::guid::generate()};
    auto text = gneiss::engine::serialization::json::encode(added);
    check(bool(text) && text->find("\"added\":{") != std::string::npos);
    auto decoded = gneiss::engine::serialization::json::decode<e::prefab_component_override>(*text);
    check(decoded && decoded->added && (*decoded->added)->get<movement_description>()->speed == 4);
    added.added.reset();
    text = gneiss::engine::serialization::json::encode(added);
    check(bool(text) && text->find("\"added\":null") != std::string::npos);
    decoded = gneiss::engine::serialization::json::decode<e::prefab_component_override>(*text);
    check(decoded && !decoded->added);
    const auto prefix = "{\"source_node_uuid\":\"" + root_id.string() + "\",\"type_guid\":\"" +
                        type.string() + "\"";
    check(!gneiss::engine::serialization::json::decode<e::prefab_component_override>(prefix + "}"));
    check(!gneiss::engine::serialization::json::decode<e::prefab_component_override>(
        "{\"source_node_uuid\":\"" + root_id.string() + "\",\"$type_guid\":\"" + type.string() +
        "\"}"));
    added.added = e::component_description{};
    // engaged optional 的空引用会与缺失 optional 共用 null，格式层仍拒绝该歧义。
    check(!gneiss::engine::serialization::json::encode_bytes(added));
    check(e::validate_component_override(added).error == e::error::invalid_argument);
    added.added = e::make_component_description(e::camera_description{});
    check(bool(gneiss::engine::serialization::json::encode_bytes(added)));
    check(e::validate_component_override(added).error == e::error::invalid_argument);
  }
  auto source = std::make_shared<e::prefab_description>();
  source->uuid = e::guid::from_bytes({87, 1});
  source->root_uuid = root_id;
  e::game_object_description root;
  root.uuid = root_id;
  source->objects.push_back(root);
  auto child = root;
  child.uuid = child_id;
  child.parent.object = root_id;
  child.components.set(movement_description{});
  source->objects.push_back(child);
  e::prefab_reference_description reference{.uri = "asset://movement.prefab", .overrides = {}};
  e::prefab_description effective;
  e::scene_diagnostic diagnostic;
  reference.component_overrides.push_back(
      {root_id, type, e::make_component_description(movement_description{4}), e::guid::generate()});
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
  reference.component_overrides.push_back({child_id, type, {}, child.components.identity(type)});
  check(e::prepare_prefab_reference(*source, reference, effective).ok() &&
        effective.objects[1].components.empty());
  // 同类型重建是新组件，旧移除覆盖必须冲突，不能删除重建后的组件。
  auto recreated = *source;
  recreated.objects[1].components.erase(type);
  recreated.objects[1].components.set(movement_description{9});
  check(recreated.objects[1].components.identity(type) != child.components.identity(type));
  auto stale_removal = e::replay_prefab_reference(recreated, reference);
  check(stale_removal && stale_removal->conflicts.size() == 1 &&
        stale_removal->description.objects[1].components.get<movement_description>()->speed == 9);
  auto deleted = *source;
  deleted.objects[1].components.erase(type);
  check(e::prepare_prefab_reference(deleted, reference, effective) == e::error::not_found);
  check(e::prepare_prefab_reference(*source, reference, effective).ok() &&
        effective.objects[1].components.empty());
  deleted.objects.pop_back();
  check(e::prepare_prefab_reference(deleted, reference, effective, &diagnostic) ==
        e::error::not_found);
  reference.overrides.push_back(
      {.key = {.target = {.source_node_uuid = child_id,
                          .target_type_guid = type,
                          .component_uuid = child.components.identity(type)},
               .path = {e::meta::property_id{1}}},
       .operation = e::value{6.0F}});
  check(e::prepare_prefab_reference(*source, reference, effective) == e::error::invalid_argument);
  reference.component_overrides.clear();
  check(e::prepare_prefab_reference(*source, reference, effective).ok());
  check(effective.objects[1].components.get<movement_description>()->speed == 6);
  auto stale_property = e::replay_prefab_reference(recreated, reference);
  check(stale_property && stale_property->conflicts.size() == 1 &&
        stale_property->description.objects[1].components.get<movement_description>()->speed == 9);
  deleted = *source;
  deleted.objects[1].components.erase(type);
  check(e::prepare_prefab_reference(deleted, reference, effective, &diagnostic) ==
        e::error::not_found);
  reference.overrides[0].key.path.front() = e::meta::property_id{99};
  check(e::prepare_prefab_reference(*source, reference, effective, &diagnostic).failed());
  check(diagnostic.path.find("99") != std::string::npos);
  reference.overrides[0].key.path.front() = e::meta::property_id{1};
  reference.overrides[0].operation = e::value{std::string("wrong")};
  check(e::prepare_prefab_reference(*source, reference, effective).failed());
  reference.overrides.clear();
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create({}), app).ok());
  auto& world = *app->native_world();
  ed::scene_edit_session document;
  check(document.create_empty(*app, &world).ok());
  e::game_object_description instance;
  instance.uuid = e::guid::from_bytes({87, 4});
  instance.prefab = reference;
  std::array<e::scene_mesh_binding, 2> bindings{};
  gneiss::scene_node_id root_node;
  check(gneiss::test_support::take_value(
            gneiss::test_support::place_document_prefab(
                document, instance, e::prefab_asset_ref{source}, bindings, false),
            root_node)
            .ok());
  check(document.validate_selection().ok());
  auto child_node = document.find_prefab_source(instance.uuid, child_id);
  check(bool(child_node));
  auto node = child_node->node;
  // 组件列表编辑生成整列表快照，来源描述和快照不会随后续修改而改变。
  {
    movement_description edited;
    edited.samples = {4, 5, 6};
    check(document.set_component_description(node, e::make_component_description(edited)).ok());
    auto changes = document.capture_component_override(
        document.find_prefab_source(instance.uuid, child_id)->node, type);
    check(changes && changes->properties.size() == 1 &&
          changes->properties[0].key.path.size() == 1 &&
          changes->properties[0].replacement().kind() == e::value_kind::sequence);
    edited.samples.clear();
    check(document.components(document.find_prefab_source(instance.uuid, child_id)->node)
              ->get<movement_description>()
              ->samples.size() == 3);
    check(
        document
            .set_component_description(node, e::make_component_description(movement_description{}))
            .ok());
    changes = document.capture_component_override(
        document.find_prefab_source(instance.uuid, child_id)->node, type);
    check(changes && changes->properties.empty());
  }
  // 编辑入口直接生成同一稳定列表上的多条插入操作，恢复源内容会删除覆盖。
  {
    movement_description edited;
    edited.waypoints = {{root_id, 4}, {child_id, 8}};
    check(document.set_component_description(node, e::make_component_description(edited)).ok());
    auto changes = document.capture_component_override(
        document.find_prefab_source(instance.uuid, child_id)->node, type);
    check(changes && changes->properties.size() == 2);
    check(std::holds_alternative<e::insert_value_operation>(changes->properties[0].operation) &&
          std::holds_alternative<e::insert_value_operation>(changes->properties[1].operation));
    auto encoded = e::serialization::json::encode(changes->properties);
    check(bool(encoded));
    auto decoded = e::serialization::json::decode<std::vector<e::prefab_property_override>>(
        std::string_view{*encoded});
    check(decoded && decoded->size() == 2);
    e::prefab_description applied;
    e::prefab_reference_description replay{.uri = "asset://movement.prefab", .overrides = *decoded};
    check(e::prepare_prefab_reference(*source, replay, applied).ok());
    check(applied.objects[1].components.get<movement_description>()->waypoints == edited.waypoints);
    check(
        document
            .set_component_description(node, e::make_component_description(movement_description{}))
            .ok());
    changes = document.capture_component_override(
        document.find_prefab_source(instance.uuid, child_id)->node, type);
    check(changes && changes->properties.empty());
  }
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
  auto state = document.capture_component_override(
      document.find_prefab_source(instance.uuid, root_id)->node, type);
  check(state && (*state->added)->get<movement_description>()->speed == 5 &&
        state->properties.empty());
  check(action(ed::prefab_component_action::remove).ok());
  state = document.capture_component_override(
      document.find_prefab_source(instance.uuid, root_id)->node, type);
  check(state && !state->added && !state->removed && state->properties.empty());
  check(history.undo().ok());
  check(document.components(document.find_prefab_source(instance.uuid, root_id)->node)
            ->get<movement_description>()
            ->speed == 5);
  check(history.undo().ok());
  check(document.components(document.find_prefab_source(instance.uuid, root_id)->node)
            ->get<movement_description>()
            ->speed == 1);
  check(history.redo().ok());
  check(history.clear().ok());
  target.source_uuid = child_id;
  check(ed::edit_component_description(document, history, world, target,
                                       {.path = {e::meta::property_id{1}}, .data = e::value{7.0F}},
                                       0)
            .ok());
  check(action(ed::prefab_component_action::remove).ok());
  state = document.capture_component_override(
      document.find_prefab_source(instance.uuid, child_id)->node, type);
  check(state && state->removed && state->properties.empty());
  check(history.undo().ok() &&
        document.components(document.find_prefab_source(instance.uuid, child_id)->node)
                ->get<movement_description>()
                ->speed == 7);
  check(history.redo().ok());
  check(action(ed::prefab_component_action::restore_source).ok());
  check(document.components(document.find_prefab_source(instance.uuid, child_id)->node)
            ->get<movement_description>()
            ->speed == 1);
  check(
      history.undo().ok() &&
      !document.components(document.find_prefab_source(instance.uuid, child_id)->node)->find(type));
  // 保存的是增删覆盖，来源资源内容保持不变。
  auto document_bytes = document.serialize();
  check(document_bytes.has_value());
  auto saved_result = gneiss::engine::asset_file::deserialize<e::scene_description>(
      std::as_bytes(std::span{document_bytes->data(), document_bytes->size()}));
  check(saved_result.has_value());
  auto saved = *saved_result;
  check(saved.objects[0].prefab->component_overrides.size() == 2);
  auto bytes = gneiss::engine::asset_file::serialize_bytes(saved);
  check(bytes.has_value());
  auto decoded = gneiss::engine::asset_file::deserialize<e::scene_description>(*bytes);
  check(decoded.has_value());
  check(decoded->objects[0].prefab->component_overrides.size() == 2);
  auto contradictory = saved;
  contradictory.objects[0].prefab->overrides.push_back(
      {.key = {.target = {.source_node_uuid = root_id,
                          .target_type_guid = type,
                          .component_uuid =
                              saved.objects[0].prefab->component_overrides.front().component_uuid},
               .path = {e::meta::property_id{1}}},
       .operation = e::value{2.0F}});
  check(!gneiss::engine::asset_file::serialize_bytes(contradictory));
  e::scene_preparation_inputs inputs;
  inputs.scene = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(*decoded)};
  inputs.prefabs.emplace(reference.uri, e::prefab_asset_ref{source});
  e::scene_instantiation_data prepared;
  check(e::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {}).ok());
  auto receipt = gneiss::test_support::scene_fixture::instantiate_scene(*app->native_world(),
                                                                        std::move(prepared), {});
  check(bool(receipt));
  const auto loaded = receipt->instance;
  const auto root_binding = std::ranges::find_if(receipt->objects, [&](const auto& object) {
    return object.object == instance.uuid && object.source == source->root_uuid;
  });
  check(root_binding != receipt->objects.end());
  auto loaded_root = root_binding->node;
  check(e::capture_components(world, loaded_root)->get<movement_description>()->speed == 5);
  check(gneiss::test_support::scene_access{*app}->unload(loaded).ok());
  gneiss::scene_node_id unrelated;
  check(document.create_node("unrelated", {}, unrelated).ok());
  const auto unrelated_entity = document.query_node(unrelated)->entity;
  const auto stable_root = document.find_prefab_root(instance.uuid)->node;
  const auto stable_child = document.find_prefab_source(instance.uuid, child_id)->node;
  const auto refresh = [&](e::prefab_asset_ref replacement) {
    auto next = document.capture_content();
    if (!next)
      return next.error();
    auto content = e::capture_prefab_record(*replacement);
    if (!content)
      return content.error();
    next->prefabs.at(reference.uri).content = *content;
    auto status = document.accept_content(*next);
    if (status.ok()) {
      check(document.projection_status().ok());
      check(document.query_node(unrelated).has_value() &&
            document.query_node(unrelated)->entity == unrelated_entity);
      root_node = document.find_prefab_root(instance.uuid)->node;
      node = document.find_prefab_source(instance.uuid, child_id)->node;
      check(root_node == stable_root && node == stable_child);
    }
    return status;
  };
  // 来源新增组件与本地新增相撞：采用来源组件，保留本地覆盖并报告冲突。
  auto updated = std::make_shared<e::prefab_description>(collision);
  check(refresh(e::prefab_asset_ref{updated}).ok());
  check(document.components(document.find_prefab_source(instance.uuid, root_id)->node)
            ->get<movement_description>()
            ->speed == 1);
  // 冲突属于文档组合结果，不从运行实例的历史来源副本读取。
  auto current_document = document.capture_content();
  check(current_document.has_value());
  auto composed = ed::compose_scene_document(*current_document);
  check(composed.has_value());
  const auto& conflicts = composed->conflicts.at(instance.uuid);
  check(conflicts.size() == 1 &&
        conflicts.front().section == e::prefab_override_section::component);
  check(
      !document.components(document.find_prefab_source(instance.uuid, child_id)->node)->find(type));
  // 来源对象改名、改默认值不会改变移除覆盖；组件消失后又出现仍隐藏。
  updated = std::make_shared<e::prefab_description>(*source);
  updated->objects[1].name = "renamed";
  updated->objects[1].components.erase(type);
  check(refresh(e::prefab_asset_ref{updated}).ok());
  check(
      !document.components(document.find_prefab_source(instance.uuid, child_id)->node)->find(type));
  // 来源成员增删与父子反转保留仍然存在的来源节点。
  e::game_object_description extra_source;
  extra_source.uuid = e::guid::generate();
  extra_source.parent.object = child_id;
  extra_source.name = "extra";
  updated->objects.push_back(extra_source);
  check(refresh(e::prefab_asset_ref{updated}).ok());
  const auto extra_node = document.find_prefab_source(instance.uuid, extra_source.uuid)->node;
  updated = std::make_shared<e::prefab_description>(*updated);
  updated->objects[1].parent.object = extra_source.uuid;
  updated->objects.back().parent.object = root_id;
  check(refresh(e::prefab_asset_ref{updated}).ok());
  check(document.find_prefab_source(instance.uuid, extra_source.uuid)->node == extra_node);
  check(refresh(e::prefab_asset_ref{source}).ok());
  check(!document.find_prefab_source(instance.uuid, extra_source.uuid));
  check(
      !document.components(document.find_prefab_source(instance.uuid, child_id)->node)->find(type));
  check(history.clear().ok());
  check(action(ed::prefab_component_action::restore_source).ok());
  check(document.components(document.find_prefab_source(instance.uuid, child_id)->node)
            ->get<movement_description>()
            ->speed == 1);
  // 同次增加普通父对象、Prefab 和自有子对象，描述顺序不必是父节点在前。
  {
    auto mixed = document.capture_content();
    check(mixed.has_value());
    auto description = mixed->description();
    check(description.has_value());
    e::game_object_description parent;
    parent.uuid = e::guid::generate();
    auto extra = description->objects.front();
    extra.uuid = e::guid::generate();
    extra.parent.object = parent.uuid;
    e::game_object_description attached;
    attached.uuid = e::guid::generate();
    attached.parent = {extra.uuid, child_id};
    description->objects.push_back(attached);
    description->objects.push_back(extra);
    description->objects.push_back(parent);
    mixed->content = *e::capture_scene_record(*description);
    ed::editor_command_history mixed_history;
    check(ed::execute_scene_edit(mixed_history, document, "混合子树", [&] {
            return document.accept_content(*mixed);
          }).ok());
    check(document.projection_status().ok());
    check(document.find_prefab_root(instance.uuid)->node == stable_root);
    check(document.query_node(unrelated)->entity == unrelated_entity);
    check(mixed_history.undo().ok() && document.projection_status().ok());
    check(!document.find_prefab_root(extra.uuid));
    check(mixed_history.redo().ok() && document.projection_status().ok());
    const auto retained = document.find_node(attached.uuid)->node;
    check(document.query_node(retained)->parent ==
          document.find_prefab_source(extra.uuid, child_id)->node);
    // 同一实例内切换来源父节点，撤销与重做只重挂接自有节点。
    check(ed::execute_scene_edit(mixed_history, document, "移动自有节点", [&] {
            return document.reparent_node(retained, document.find_prefab_root(extra.uuid)->node);
          }).ok());
    check(mixed_history.undo().ok() && document.projection_status().ok());
    check(document.find_node(attached.uuid)->node == retained);
    check(document.query_node(retained)->parent ==
          document.find_prefab_source(extra.uuid, child_id)->node);
    check(mixed_history.redo().ok() && document.projection_status().ok());
    check(document.query_node(retained)->parent == document.find_prefab_root(extra.uuid)->node);
    // 新建、复制、子树删除恢复均保留具体来源父地址。
    const auto source_parent = document.find_prefab_source(extra.uuid, child_id)->node;
    gneiss::scene_node_id created;
    check(document.create_node("自有子节点", source_parent, created).ok());
    check(document.query_node(created)->parent == source_parent);
    gneiss::scene_node_id duplicate;
    check(document.duplicate_subtree(created, source_parent, duplicate).ok());
    check(duplicate != created && document.query_node(duplicate)->parent == source_parent);
    ed::scene_subtree_snapshot subtree;
    check(document.destroy_subtree(duplicate, subtree).ok());
    check(subtree.parent == e::object_address{extra.uuid, child_id});
    gneiss::scene_node_id restored;
    check(document.restore_subtree(subtree, restored).ok());
    check(document.query_node(restored)->parent == source_parent);
    check(document.destroy_subtree(restored, subtree).ok());
    check(document.destroy_subtree(created, subtree).ok());
    // 删除 Prefab，同时将自有子节点移到新父对象，子节点实体保持。
    auto detached = document.capture_content();
    auto data = detached->content.get();
    std::erase_if(data.objects, [&](const auto& object) { return object.uuid == extra.uuid; });
    for (auto& object : data.objects)
      if (object.uuid == attached.uuid)
        object.parent = {parent.uuid, {}};
    detached->content = e::scene_record{data};
    check(document.accept_content(*detached).ok() && document.projection_status().ok());
    check(document.find_node(attached.uuid)->node == retained);
    check(document.query_node(unrelated)->entity == unrelated_entity);
    check(document.find_prefab_root(instance.uuid)->node == stable_root);
  }
  check(source->objects[0].components.empty() &&
        source->objects[1].components.get<movement_description>()->speed == 1);
  return 0;
} catch (const std::exception& failure) {
  std::fprintf(stderr, "%s\n", failure.what());
  return 1;
}
