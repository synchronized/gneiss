// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/author_property_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"

#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/scene/component_adapter.hpp"
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
namespace {
engine::error component_lifecycle(scene_document& document, editor_command_history& history,
                                  engine::guid object, engine::guid type, bool adding) {
  using namespace engine;
  const auto* node = document.find_node(object);
  if (!node)
    return error::not_found;
  auto source = [&]() -> result<component_description> {
    if (adding)
      return default_component_description(type);
    auto current = document.components(node->node);
    if (!current)
      return current.error();
    auto* found = current->find(type);
    return found ? result<component_description>{*found}
                 : result<component_description>{error::not_found};
  }();
  if (!source)
    return source.error();
  auto choices = component_editor_types();
  if (!choices)
    return choices.error();
  bool removable = false;
  for (const auto& choice : *choices)
    if (choice.type == type)
      removable = choice.removable;
  if (!removable)
    return error::unsupported;
  auto dependencies = component_dependencies({*source});
  if (!dependencies)
    return dependencies.error();
  // 描述无法保留旧的渲染资源实例；有资源依赖的操作走资产历史。
  if (!dependencies->empty())
    return error::unsupported;
  auto add = [&document, object, value = *source]() -> error {
    const auto* target = document.find_node(object);
    return target ? document.add_component_description(target->node, value) : error::not_found;
  };
  auto remove = [&document, object, value = *source]() -> error {
    const auto* target = document.find_node(object);
    if (!target)
      return error::not_found;
    auto current = document.components(target->node);
    if (!current)
      return current.error();
    auto* found = current->find(value.guid());
    if (!found)
      return error::not_found;
    if (!equal_components({*found}, {value}))
      return error::invalid_state;
    return document.remove_component_description(target->node, value.guid());
  };
  if (adding)
    return history.execute({.label = "添加组件", .undo = remove, .redo = add, .merge_key = {}});
  return history.execute({.label = "移除组件", .undo = add, .redo = remove, .merge_key = {}});
}
}
engine::error add_component_description(scene_document& document, editor_command_history& history,
                                        engine::guid object, engine::guid type) noexcept try {
  return component_lifecycle(document, history, object, type, true);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error remove_component_description(scene_document& document,
                                           editor_command_history& history, engine::guid object,
                                           engine::guid type) noexcept try {
  return component_lifecycle(document, history, object, type, false);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error edit_prefab_component_structure(scene_document& document,
                                              editor_command_history& history,
                                              const component_edit_target& identity,
                                              prefab_component_action action) noexcept try {
  using namespace engine;
  if (!identity.instance_uuid || !identity.source_uuid)
    return error::invalid_argument;
  auto location = locate_component(document, identity);
  if (!location)
    return location.error();
  auto before = document.capture_component_override(location->first, identity.component_type);
  if (!before)
    return before.error();
  auto components = document.components(location->first);
  if (!components)
    return components.error();
  const auto* component = components->find(identity.component_type);
  prefab_component_state after;
  if (action == prefab_component_action::add) {
    if (component)
      return error::invalid_state;
    auto value = default_component_description(identity.component_type);
    if (!value)
      return value.error();
    auto dependencies = component_dependencies({*value});
    if (!dependencies)
      return dependencies.error();
    if (!dependencies->empty())
      return error::unsupported;
    after.added = std::move(*value);
  } else if (action == prefab_component_action::remove) {
    if (!component)
      return error::not_found;
    after.removed = !before->added;
  }
  auto apply = [&document, identity](const prefab_component_state& state) {
    auto target = locate_component(document, identity);
    return target ? document.set_component_override(target->first, identity.component_type, state)
                  : target.error();
  };
  return history.execute({.label = action == prefab_component_action::add      ? "添加实例组件"
                                   : action == prefab_component_action::remove ? "移除实例组件"
                                                                               : "恢复来源组件",
                          .undo = [apply, value = *before] { return apply(value); },
                          .redo = [apply, after] { return apply(after); },
                          .merge_key = {}});
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
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
  auto policy = component_editor(identity.component_type);
  if (!policy)
    return policy.error();
  if (policy->route != component_editor_route::reflected)
    return error::unsupported;
  {
    auto components = document.components(location->first);
    if (!components)
      return components.error();
    const auto* current = components->find(identity.component_type);
    if (!current)
      return error::not_found;
    auto before = *current;
    auto type = meta::find_type(identity.component_type);
    if (!type)
      return type.error();
    for (const auto& step : edit.path) {
      if (const auto* id = std::get_if<meta::property_id>(&step)) {
        auto fields = type->properties();
        if (!fields)
          return fields.error();
        auto field = std::ranges::find(*fields, *id, &meta::property_info::id);
        if (field == fields->end())
          return error::not_found;
        if (has_flags(field->flags, meta::property_flags::editor_read_only) ||
            has_flags(field->flags, meta::property_flags::read_only))
          return error::unsupported;
        type = field->type;
      } else {
        type = type->element_type();
        if (!type)
          return type.error();
      }
    }
    auto changed = modify_component(before, [&](meta::mutable_instance root) -> error {
      auto target = meta::resolve_path(root, edit.path);
      if (!target)
        return target.error();
      if (!edit.object)
        return target->write_value(edit.data);
      auto registry = meta::default_registry();
      if (!registry)
        return registry.error();
      auto source = meta::instance::from_object(*registry, edit.object_type, edit.object.get());
      return source ? target->assign(*source) : source.error();
    });
    if (!changed)
      return changed.error();
    auto original = before.encode(), next = changed->encode();
    if (!original)
      return original.error().error;
    if (!next)
      return next.error().error;
    if (*original == *next)
      return error::success;
    const auto apply = [&document, &world, identity](const component_description& value) {
      auto node = locate_component(document, identity);
      if (!node)
        return node.error();
      auto prepared = prepare_component_editor_value(world, node->first, value);
      return prepared ? document.set_component_description(node->first, *prepared)
                      : prepared.error();
    };
    std::string key = identity.component_type.string() + identity.object_uuid.string() +
                      identity.instance_uuid.string() + identity.source_uuid.string();
    for (const auto& step : edit.path) {
      if (const auto* field = std::get_if<meta::property_id>(&step))
        key += "/p" + std::to_string(field->get());
      else
        key += "/i" + std::to_string(std::get<std::size_t>(step));
    }
    return history.execute(
        {.label = "修改组件属性",
         .undo = [apply, before] { return apply(before); },
         .redo = [apply, after = std::move(*changed)] { return apply(after); },
         .merge_key = serial && !edit.object ? key + ":" + std::to_string(serial) : std::string{}});
  }
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
} // namespace gneiss::editor
