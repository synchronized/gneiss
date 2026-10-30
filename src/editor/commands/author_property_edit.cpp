// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/author_property_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"

#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/serialization.hpp"
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace gneiss::editor {
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
template <class T, class Apply>
engine::error edit_description(editor_command_history& history,
                               const component_edit_target& identity, const property_edit& edit,
                               std::uint64_t serial, const T& before, Apply apply) {
  using namespace engine;
  error status = error::success;
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
  auto original = meta::serialize(before);
  if (!original)
    return original.error().error;
  if (*original == *validated)
    return error::success;
  return history.execute(
      {.label = "修改组件属性",
       .undo = [apply, before] { return apply(before); },
       .redo = [apply, candidate] { return apply(candidate); },
       .merge_key = serial && !edit.object
                        ? identity.component_type.string() + ":" + identity.object_uuid.string() +
                              identity.instance_uuid.string() + identity.source_uuid.string() +
                              key + ":" + std::to_string(serial)
                        : std::string{}});
}
}
engine::error edit_component_description(scene_document& document, editor_command_history& history,
                                         engine::world& world,
                                         const component_edit_target& identity,
                                         const property_edit& edit, std::uint64_t serial) noexcept
    try {
  using namespace engine;
  if (edit.path.empty() && !edit.object)
    return error::invalid_argument;
  auto location = locate_component(document, identity);
  if (!location)
    return location.error();
  if (identity.component_type == transform_component_type) {
    math::transform before;
    auto status = world.node_get_local_transform(location->first, before);
    if (status.failed())
      return status;
    const auto apply = [&document, identity](const math::transform& value) {
      auto location = locate_component(document, identity);
      return location ? document.set_local_transform(location->first, value) : location.error();
    };
    return edit_description(history, identity, edit, serial, before, apply);
  }
  if (identity.component_type != camera_description::type_guid)
    return error::unsupported;
  if (edit.path.empty() || !std::holds_alternative<meta::property_id>(edit.path.front()) ||
      std::get<meta::property_id>(edit.path.front()) != meta::property_id{1})
    return error::unsupported;
  camera_description before;
  auto status = world.entity_get_camera(location->second, before.settings);
  if (status.failed())
    return status;
  entity_id active;
  status = world.get_active_camera(active);
  if (status.failed() && status != error::not_ready)
    return status;
  before.is_primary = active == location->second;
  const auto apply = [&document, &world, identity](const camera_description& value) {
    auto location = locate_component(document, identity);
    if (!location)
      return location.error();
    entity_id active;
    auto status = world.get_active_camera(active);
    if (status.failed() && status != error::not_ready)
      return status;
    auto next = value;
    next.is_primary = active == location->second;
    return document.set_camera(location->first, next);
  };
  return edit_description(history, identity, edit, serial, before, apply);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
} // namespace gneiss::editor
