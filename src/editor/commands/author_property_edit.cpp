// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/author_property_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"

#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/value.hpp"
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
    const auto node = document.find_prefab_source(target.instance_uuid, target.source_uuid);
    if (node)
      return std::pair{node->node, node->entity};
  } else {
    const auto node = document.find_node(target.object_uuid);
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
  std::string key = meta::property_path_text(edit.path);
  target = meta::resolve_path(*target, edit.path);
  if (!target)
    return target.error();
  if (edit.object) {
    auto source = meta::instance::from_object(edit.object_type, edit.object.get());
    if (!source)
      return source.error();
    status = target->assign(*source);
  } else
    status = target->write_value(edit.data);
  if (status.failed())
    return status;
  auto validated = serialization::capture_value(candidate);
  if (!validated)
    return validated.error().error;
  auto original = serialization::capture_value(before);
  if (!original)
    return original.error().error;
  if (*original == *validated)
    return error::success;
  return history.execute(
      {.label = "修改组件属性",
       .undo =
           [apply, original = std::move(*original)] {
             auto restored = serialization::restore_value<T>(original);
             return restored ? apply(*restored) : restored.error().error;
           },
       .redo =
           [apply, validated = std::move(*validated)] {
             auto restored = serialization::restore_value<T>(validated);
             return restored ? apply(*restored) : restored.error().error;
           },
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
  const auto node = document.find_node(object);
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
  auto frozen = serialization::capture_value(*source);
  if (!frozen)
    return frozen.error().error;
  auto add = [&document, object, content = *frozen]() -> error {
    auto value = serialization::restore_value<component_description>(content);
    if (!value)
      return value.error().error;
    const auto target = document.find_node(object);
    return target ? document.add_component_description(target->node, *value) : error::not_found;
  };
  auto remove = [&document, object, type, content = *frozen]() -> error {
    const auto target = document.find_node(object);
    if (!target)
      return error::not_found;
    auto current = document.components(target->node);
    if (!current)
      return current.error();
    auto* found = current->find(type);
    if (!found)
      return error::not_found;
    auto current_value = serialization::capture_value(*found);
    if (!current_value)
      return current_value.error().error;
    if (*current_value != content)
      return error::invalid_state;
    return document.remove_component_description(target->node, type);
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
  // 覆盖操作本身不可变；新增组件按内容冻结，避免历史持有可变描述别名。
  auto freeze_added = [](prefab_component_state& state) -> result<value> {
    auto content = serialization::capture_value(state.added.value_or(component_description{}));
    if (!content)
      return content.error().error;
    state.added.reset();
    return *content;
  };
  auto before_content = freeze_added(*before);
  auto after_content = freeze_added(after);
  if (!before_content)
    return before_content.error();
  if (!after_content)
    return after_content.error();
  auto apply = [&document, identity](prefab_component_state state, const value& content) {
    if (content.kind() != value_kind::invalid) {
      auto added = serialization::restore_value<component_description>(content);
      if (!added)
        return added.error().error;
      state.added = std::move(*added);
    }
    auto target = locate_component(document, identity);
    return target ? document.set_component_override(target->first, identity.component_type, state)
                  : target.error();
  };
  return history.execute(
      {.label = action == prefab_component_action::add      ? "添加实例组件"
                : action == prefab_component_action::remove ? "移除实例组件"
                                                            : "恢复来源组件",
       .undo = [apply, state = *before,
                content = *before_content] { return apply(state, content); },
       .redo = [apply, after, content = *after_content] { return apply(after, content); },
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
      if (!edit.object) {
        if (edit.data.kind() != value_kind::object)
          return target->write_value(edit.data);
        auto captured = serialization::capture_value(edit.data);
        if (!captured)
          return captured.error().error;
        auto owned = serialization::restore_value<value>(*captured);
        return owned ? target->write_value(*owned) : owned.error().error;
      }
      auto source = meta::instance::from_object(edit.object_type, edit.object.get());
      return source ? target->assign(*source) : source.error();
    });
    if (!changed)
      return changed.error();
    auto before_view = before->view(), after_view = (*changed)->view();
    if (!before_view)
      return before_view.error();
    if (!after_view)
      return after_view.error();
    auto before_property = meta::resolve_path(*before_view, edit.path);
    auto after_property = meta::resolve_path(*after_view, edit.path);
    if (!before_property)
      return before_property.error();
    if (!after_property)
      return after_property.error();
    auto original = serialization::capture_value(*before_property),
         next = serialization::capture_value(*after_property);
    if (!original)
      return original.error().error;
    if (!next)
      return next.error().error;
    if (*original == *next)
      return error::success;
    const auto apply = [&document, &world, identity,
                        path = edit.path](const engine::value& content) {
      auto node = locate_component(document, identity);
      if (!node)
        return node.error();
      auto current = document.components(node->first);
      if (!current)
        return current.error();
      auto found = current->find(identity.component_type);
      if (!found)
        return error::not_found;
      // 历史只还原本次编辑的路径，不能覆盖之后独立修改的其他属性。
      auto restored = modify_component(*found, [&](meta::mutable_instance root) -> error {
        auto target = meta::resolve_path(root, path);
        if (!target)
          return target.error();
        return serialization::apply_value(*target, content).error;
      });
      if (!restored)
        return restored.error();
      auto prepared = prepare_component_editor_value(world, node->first, *restored);
      return prepared ? document.set_component_description(node->first, *prepared)
                      : prepared.error();
    };
    std::string key = identity.component_type.string() + identity.object_uuid.string() +
                      identity.instance_uuid.string() + identity.source_uuid.string();
    key += meta::property_path_text(edit.path);
    return history.execute(
        {.label = "修改组件属性",
         .undo = [apply, content = std::move(*original)] { return apply(content); },
         .redo = [apply, content = std::move(*next)] { return apply(content); },
         .merge_key = serial && !edit.object ? key + ":" + std::to_string(serial) : std::string{}});
  }
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
} // namespace gneiss::editor
