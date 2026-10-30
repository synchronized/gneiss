// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/author_property_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"

#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/serialization.hpp"
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace gneiss::editor {

namespace {
gneiss::engine::error write_author_property(scene_document& session,
                                            property_inspector_model& inspector,
                                            gneiss::engine::world* world, gneiss::engine::guid uuid,
                                            gneiss::engine::guid type_id,
                                            gneiss::engine::meta::property_id field_id,
                                            const gneiss::engine::value& value) noexcept {
  const auto* current = session.find_node(uuid);
  if (current == nullptr) {
    return gneiss::engine::error::not_found;
  }
  const auto operation = inspector.set_value(world, current->entity, type_id, field_id, value);
  if (operation.ok()) {
    session.mark_dirty();
  }
  return operation;
}
} // namespace

gneiss::engine::error
edit_author_property(scene_document& session, editor_command_history& history,
                     property_inspector_model& inspector, gneiss::engine::world* world,
                     const inspector_component& component, const inspector_property& property,
                     const gneiss::engine::value& value, std::uint64_t edit_serial) noexcept try {
  const auto* selected = session.selected_node();
  if (selected == nullptr) {
    return gneiss::engine::error::invalid_state;
  }
  const auto uuid = selected->uuid;
  const auto type_id = component.type_id;
  const auto field_id = property.id;
  const auto previous = property.value;
  std::string merge_key = "property:" + uuid.string();
  merge_key.append(reinterpret_cast<const char*>(type_id.bytes().data()), sizeof(type_id.bytes()));
  const auto field_number = field_id.get();
  merge_key.append(reinterpret_cast<const char*>(&field_number), sizeof(field_number));
  merge_key.append(reinterpret_cast<const char*>(&edit_serial), sizeof(edit_serial));
  // 命令对象分配先于写入，避免分配失败留下未记录的属性变更。
  auto command = std::make_unique<functional_editor_command>(editor_command_history::command{
      .label = std::string{"修改 "} + property.name,
      .undo =
          [&session, &inspector, world, uuid, type_id, field_id, previous]() noexcept {
            return write_author_property(session, inspector, world, uuid, type_id, field_id,
                                         previous);
          },
      .redo =
          [&session, &inspector, world, uuid, type_id, field_id, value]() noexcept {
            return write_author_property(session, inspector, world, uuid, type_id, field_id, value);
          },
      .merge_key = std::move(merge_key),
  });
  const auto operation = inspector.set_value(type_id, field_id, value);
  if (operation.failed()) {
    return operation;
  }
  const auto recorded = history.record(std::move(command));
  if (recorded.failed()) {
    (void)inspector.set_value(type_id, field_id, previous);
    return recorded;
  }
  session.mark_dirty();
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

namespace {
engine::result<std::pair<scene_node_id, entity_id>>
locate_component(scene_document& document, const component_edit_target& target) {
  if (target.instance_uuid) {
    const auto* node = document.find_prefab_source(target.instance_uuid, target.source_uuid);
    if (node)
      return std::pair{node->node, node->entity};
  } else {
    const auto* node = document.find_node(target.object_uuid);
    if (node)
      return std::pair{node->node, node->entity};
  }
  return engine::error::not_found;
}
}
engine::error edit_component_description(scene_document& document, editor_command_history& history,
                                         engine::world& world,
                                         const component_edit_target& identity,
                                         const property_edit& edit, std::uint64_t serial) noexcept
    try {
  using namespace engine;
  if (identity.component_type != camera_description::type_guid)
    return error::unsupported;
  if (edit.path.empty() || !std::holds_alternative<meta::property_id>(edit.path.front()) ||
      std::get<meta::property_id>(edit.path.front()) != meta::property_id{1})
    return error::unsupported;
  auto location = locate_component(document, identity);
  if (!location)
    return location.error();
  camera_description before;
  auto status = world.entity_get_camera(location->second, before.settings);
  if (status.failed())
    return status;
  entity_id active;
  status = world.get_active_camera(active);
  if (status.failed() && status != error::not_ready)
    return status;
  before.is_primary = active == location->second;
  auto candidate = before;
  auto target = meta::mutable_instance::from(candidate);
  if (!target)
    return target.error();
  std::string key;
  for (const auto& step : edit.path) {
    if (const auto* property = std::get_if<meta::property_id>(&step)) {
      target = target->property(*property);
      key += "/p" + std::to_string(property->get());
    } else {
      auto sequence = target->as_sequence();
      if (!sequence)
        return sequence.error();
      const auto index = std::get<std::size_t>(step);
      target = sequence->at(index);
      key += "/i" + std::to_string(index);
    }
    if (!target)
      return target.error();
  }
  if (edit.object) {
    auto registry = meta::default_registry();
    if (!registry)
      return registry.error();
    auto source = meta::instance::from_object(*registry, edit.object_type, edit.object.get());
    if (!source)
      return source.error();
    status = target->assign(*source);
  } else
    status = target->write_value(edit.data);
  if (status.failed())
    return status;
  auto validated = meta::serialize(candidate);
  if (!validated)
    return validated.error().error;
  if (before.settings.fov == candidate.settings.fov &&
      before.settings.near == candidate.settings.near &&
      before.settings.far == candidate.settings.far)
    return error::success;
  const auto apply = [&document, &world, identity](const camera_description& value) {
    auto location = locate_component(document, identity);
    if (!location)
      return location.error();
    // 主相机是独立的场景状态，参数撤销不能顺带切换它。
    entity_id active;
    auto result = world.get_active_camera(active);
    if (result.failed() && result != error::not_ready)
      return result;
    auto next = value;
    next.is_primary = active == location->second;
    return document.set_camera(location->first, next);
  };
  return history.execute({.label = "修改 Camera 描述",
                          .undo = [apply, before] { return apply(before); },
                          .redo = [apply, candidate] { return apply(candidate); },
                          .merge_key = serial && !edit.object
                                           ? "camera:" + identity.object_uuid.string() +
                                                 identity.instance_uuid.string() +
                                                 identity.source_uuid.string() + key + ":" +
                                                 std::to_string(serial)
                                           : std::string{}});
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
} // namespace gneiss::editor
