// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/commands/author_property_edit.hpp"
#include "documents/components/component_registration.hpp"
#include "documents/scene_document_edit.hpp"
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/capabilities/sequence.hpp>
#include <gneiss/engine/core/meta/resolve_path.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/property_edit_policy.hpp>
#include <gneiss/engine/core/meta/resolve.hpp>

#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/serialization/value.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace gneiss::editor {
namespace {
gneiss::result<std::pair<entity_id, entity_id>>
locate_component(scene_edit_session& document, const component_edit_target& target) {
  if (target.instance_uuid) {
    const auto node = document.find_prefab_source(target.instance_uuid, target.source_uuid);
    if (node)
      return std::pair{node->node, node->entity};
  } else {
    const auto node = document.find_node(target.object_uuid);
    if (node)
      return std::pair{node->node, node->entity};
  }
  return gneiss::error::not_found;
}
template <class T, class Apply>
gneiss::error edit_description(scene_edit_session& document, editor_command_history& history,
                               const component_edit_target& identity, const property_edit& edit,
                               std::uint64_t serial, const T& before, Apply apply) {
  using namespace gneiss;
  error status = error::success;
  auto candidate = before;
  auto root = meta::view(candidate);
  std::string key = gneiss::property_path_text(edit.path);
  auto target = meta::resolve_path(root, edit.path);
  if (!target)
    return target.error();
  if (edit.object) {
    auto source = meta::const_instance::from_object(edit.object_type, edit.object.get());
    if (!source)
      return source.error();
    status = target->assign(*source);
  } else
    status = target->assign(edit.data);
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
  return execute_scene_edit(
      history, document, "修改组件属性", [&] { return apply(candidate); },
      serial && !edit.object
          ? identity.component_type.string() + ":" + identity.object_uuid.string() +
                identity.instance_uuid.string() + identity.source_uuid.string() + key + ":" +
                std::to_string(serial)
          : std::string{});
}
}
namespace {
gneiss::error component_lifecycle(scene_edit_session& document, editor_command_history& history,
                                  gneiss::guid object, gneiss::guid type, bool adding) {
  using namespace gneiss;
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
  return execute_scene_edit(history, document, adding ? "添加组件" : "移除组件", [&] {
    return adding ? document.add_component_description(node->node, *source)
                  : document.remove_component_description(node->node, type);
  });
}
}
gneiss::error add_component_description(scene_edit_session& document,
                                        editor_command_history& history, gneiss::guid object,
                                        gneiss::guid type) {
  return component_lifecycle(document, history, object, type, true);
}
gneiss::error remove_component_description(scene_edit_session& document,
                                           editor_command_history& history, gneiss::guid object,
                                           gneiss::guid type) {
  return component_lifecycle(document, history, object, type, false);
}
gneiss::error edit_prefab_component_structure(scene_edit_session& document,
                                              editor_command_history& history,
                                              const component_edit_target& identity,
                                              prefab_component_action action) {
  using namespace gneiss;
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
    after.component_uuid = guid::generate();
  } else if (action == prefab_component_action::remove) {
    if (!component)
      return error::not_found;
    after.removed = !before->added;
  }
  return execute_scene_edit(history, document,
                            action == prefab_component_action::add      ? "添加实例组件"
                            : action == prefab_component_action::remove ? "移除实例组件"
                                                                        : "恢复来源组件",
                            [&] {
                              return document.set_component_override(
                                  location->first, identity.component_type, after);
                            });
}
gneiss::error edit_component_description(scene_edit_session& document,
                                         editor_command_history& history, gneiss::world& world,
                                         const component_edit_target& identity,
                                         const property_edit& edit, std::uint64_t serial) {
  using namespace gneiss;
  (void)world;
  if (edit.path.empty() && !edit.object)
    return error::invalid_argument;
  auto location = locate_component(document, identity);
  if (!location)
    return location.error();
  if (identity.component_type == math::transform::type_guid) {
    auto ordinary = document.query_node(location->first);
    auto prefab = document.query_prefab_node(location->first);
    if (!ordinary && !prefab)
      return error::not_found;
    auto before = ordinary ? ordinary->local_transform : prefab->local_transform;
    const auto apply = [&document, identity](const math::transform& value) {
      auto location = locate_component(document, identity);
      return location ? document.set_local_transform(location->first, value) : location.error();
    };
    return edit_description(document, history, identity, edit, serial, before, apply);
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
    auto type = meta::resolve(identity.component_type);
    if (!type)
      return gneiss::error{gneiss::error::not_found};
    for (const auto& step : edit.path) {
      if (const auto* id = std::get_if<gneiss::property_id>(&step)) {
        auto fields = type.template capability<gneiss::meta::cap::object>().properties();
        if (!type.registered())
          return error::not_found;
        auto field = std::ranges::find(fields, *id, &meta::property::id);
        if (field == fields.end())
          return error::not_found;
        if (gneiss::is_edit_read_only(*field) ||
            !gneiss::meta::has_flags(field->capabilities(),
                                             gneiss::meta::property_capabilities::writable))
          return error::unsupported;
        type = field->type();
      } else {
        meta::type element;
        if (std::holds_alternative<std::string>(step)) {
          auto capability = type.capability<meta::cap::associative>();
          if (capability)
            element = capability.element_type();
        } else {
          auto capability = type.capability<meta::cap::sequence>();
          if (capability)
            element = capability.element_type();
        }
        if (!element)
          return error::unsupported;
        type = element;
      }
    }
    auto changed = modify_component(before, [&](meta::instance root) -> error {
      auto target = meta::resolve_path(root, edit.path);
      if (!target)
        return target.error();
      if (!edit.object) {
        if (edit.data.kind() != value_kind::object)
          return target->assign(edit.data);
        auto captured = serialization::capture_value(edit.data);
        if (!captured)
          return captured.error().error;
        auto owned = serialization::restore_value<value>(*captured);
        return owned ? target->assign(*owned) : owned.error().error;
      }
      auto source = meta::const_instance::from_object(edit.object_type, edit.object.get());
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
    auto prepared = prepare_component_editor_value(world, location->first, *changed);
    if (!prepared)
      return prepared.error();
    std::string key = identity.component_type.string() + identity.object_uuid.string() +
                      identity.instance_uuid.string() + identity.source_uuid.string();
    key += gneiss::property_path_text(edit.path);
    return execute_scene_edit(
        history, document, "修改组件属性",
        [&] { return document.set_component_description(location->first, *prepared); },
        serial && !edit.object ? key + ":" + std::to_string(serial) : std::string{});
  }
}
} // namespace gneiss::editor
