// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/export.hpp>
#include "function/scene/detail/world_component_access.hpp"
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/resource/property_edit_policy.hpp>
#include <gneiss/engine/core/serialization/value.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/resource/scene/scene_validation.hpp>
#include <map>
#include <mutex>

#include "function/scene/detail/camera_selection_state.hpp"
#include "function/scene/detail/component_storage.hpp"
namespace gneiss::detail {
template <> struct component_policy<camera_component> {
  static void removed(detail::world_component_access::storage& state, entity_id entity) {
    auto& selection = state.context<camera_selection_state>();
    if (selection.entity == entity)
      selection.entity = {};
  }
};
}

namespace gneiss {
namespace {
// 独立候选按反射内容恢复，不让内嵌 ref 的浅拷贝改写来源描述。
result<component_description> copy_candidate(const component_description& source) {
  auto content = serialization::capture_value(source);
  if (!content)
    return content.error().error;
  auto copied = serialization::restore_value<component_description>(*content);
  if (!copied)
    return copied.error().error;
  return std::move(*copied);
}
entity_id entity(world& owner, entity_id node) {
  auto object = owner.object(entity_id{node.get()});
  return object ? object->entity() : entity_id{};
}
// 与实体同寿命的身份元数据，不持有编辑内容或历史。
struct component_instance_ids {
  std::map<guid, guid> values;
};
component_instance_ids& instance_ids(world& owner, entity_id node) {
  auto& state = detail::world_component_access::get(owner);
  auto id = entity(owner, node).get();
  auto* identities = state.get<component_instance_ids>(id);
  return identities ? *identities : state.emplace<component_instance_ids>(id);
}
error camera_valid(const camera_description& value) {
  return validate_camera_description(value).error;
}
error camera_install(component_context& context, const camera_description& value) {
  auto candidate = camera_component{value.settings, value.is_primary};
  if (context.target.try_component<camera_component>()) {
    return context.target.modify_component<camera_component>(
        [&](auto& camera) noexcept { camera = std::move(candidate); });
  }
  return context.target.add_component<camera_component>(std::move(candidate));
}
result<std::optional<camera_description>> camera_capture(game_object target) {
  auto* camera = target.try_component<camera_component>();
  if (!camera)
    return std::optional<camera_description>{};
  return std::optional<camera_description>(std::in_place, camera->settings, camera->primary_hint);
}
error mesh_valid(const mesh_component_description& value) {
  return validate_mesh_component_description(value).error;
}
error mesh_install(component_context& context, const mesh_component_description& value) {
  const auto resolve = [&]<class T>(std::string_view uri) -> result<ref<const T>> {
    if (uri.empty())
      return ref<const T>{};
    if (context.lookup_resource) {
      auto resource = context.lookup_resource({std::string(uri), resource_traits<T>::kind});
      if (!resource)
        return resource.error();
      auto typed = resource_cast<T>(*resource);
      if (!typed || resource_uri(typed) != uri)
        return error::invalid_argument;
      return typed;
    }
    // 普通属性编辑保留当前资源；固定文档引用由上面的 lookup 明确提供。
    auto current = context.target.try_component<gneiss::mesh_component>();
    if (current) {
      ref<const T> existing;
      if constexpr (std::same_as<T, gneiss::mesh>)
        existing = current->mesh;
      else
        existing = current->material;
      if (resource_uri(existing) == uri)
        return existing;
    }
    if (!context.resources)
      return error::not_ready;
    auto loaded = context.resources->load({std::string(uri), resource_traits<T>::kind});
    return loaded ? result<ref<const T>>{loaded->template get<T>()}
                  : result<ref<const T>>{loaded.error()};
  };
  auto mesh = resolve.template operator()<gneiss::mesh>(value.mesh_uri);
  if (!mesh)
    return mesh.error();
  auto material = resolve.template operator()<gneiss::material>(value.material_uri);
  if (!material)
    return material.error();
  if (context.resources && ((*mesh && !context.resources->owns(*mesh)) ||
                            (*material && !context.resources->owns(*material))))
    return error::invalid_argument;
  if (context.target.try_component<mesh_component>()) {
    return context.target.modify_component<mesh_component>(
        [&](auto& current) noexcept { current = {*mesh, *material}; });
  }
  return context.target.add_component<mesh_component>(mesh_component{*mesh, *material});
}
result<std::optional<mesh_component_description>> mesh_capture(game_object target) {
  auto value = target.try_component<gneiss::mesh_component>();
  if (!value)
    return std::optional<mesh_component_description>{};
  return std::optional<mesh_component_description>{
      {std::string(resource_uri(value->mesh)), std::string(resource_uri(value->material))}};
}
struct adapter_registry {
  std::mutex mutex;
  bool frozen{};
  std::vector<component_adapter> values;
  adapter_registry() {
    values.push_back(make_component_adapter<camera_description>(
        {.validate = camera_valid,
         .install = camera_install,
         .capture = camera_capture,
         .remove =
             [](game_object target) { return target.remove_component<gneiss::camera_component>(); },
         .equal =
             [](const camera_description& a, const camera_description& b) {
               return a.settings.fov == b.settings.fov && a.settings.near_plane == b.settings.near_plane &&
                      a.settings.far_plane == b.settings.far_plane && a.is_primary == b.is_primary;
             },
         .properties = component_property_operations<camera_description>{}}));
    values.push_back(make_component_adapter<mesh_component_description>(
        {.validate = mesh_valid,
         .install = mesh_install,
         .capture = mesh_capture,
         .remove =
             [](game_object target) { return target.remove_component<gneiss::mesh_component>(); },
         .equal =
             [](const mesh_component_description& a, const mesh_component_description& b) {
               return a.mesh_uri == b.mesh_uri && a.material_uri == b.material_uri;
             },
         .dependencies =
             [](const mesh_component_description& value) {
               std::vector<render_resource_source> result;
               if (!value.mesh_uri.empty())
                 result.push_back({value.mesh_uri, render_resource_kind::mesh});
               if (!value.material_uri.empty())
                 result.push_back({value.material_uri, render_resource_kind::material});
               return result;
             },
         .properties = component_property_operations<mesh_component_description>{}}));
  }
};
adapter_registry& registry() {
  static adapter_registry value;
  return value;
}
std::span<const component_adapter> adapters() {
  auto& state = registry();
  std::lock_guard lock(state.mutex);
  state.frozen = true;
  return state.values;
}
const component_adapter* find(guid type, bool property = false) {
  for (const auto& adapter : adapters())
    if (adapter.type == type || (property && adapter.properties.property_type == type))
      return &adapter;
  return nullptr;
}
error valid_target(component_context& context) {
  if (!context.target.owner())
    return error::invalid_handle;
  if (!context.target.owner()->is_owner_thread())
    return error::invalid_state;
  return context.target.is_valid() ? error{error::success} : error{error::invalid_handle};
}
}
error resolve_component_references(component_context context,
                                   const component_descriptions& values) {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  for (const auto& value : values) {
    if (!value)
      return error::invalid_argument;
    const auto* adapter = find(value->guid());
    if (!adapter)
      return error::unsupported;
    if (adapter->resolve_references) {
      status = adapter->resolve_references(context, value);
      if (status.failed())
        return status;
    }
  }
  return error::success;
}
error register_scene_component(component_adapter adapter) {
  if (!adapter.type || !adapter.validate || !adapter.install || !adapter.capture ||
      !adapter.remove || !adapter.equal)
    return error::invalid_argument;
  if (!adapter.properties.property_type)
    adapter.properties.property_type = adapter.type;
  auto& state = registry();
  std::lock_guard lock(state.mutex);
  if (state.frozen)
    return error::invalid_state;
  for (const auto& old : state.values)
    if (old.type == adapter.type ||
        old.properties.property_type == adapter.properties.property_type ||
        old.type == adapter.properties.property_type ||
        old.properties.property_type == adapter.type)
      return error::invalid_argument;
  state.values.push_back(std::move(adapter));
  return error::success;
}
result<component_description> modify_component(const component_description& source,
                                               const std::function<error(meta::instance)>& change) {
  if (!source)
    return error::invalid_argument;
  const auto* adapter = find(source->guid());
  if (!adapter)
    return error::unsupported;
  if (!change)
    return error::invalid_argument;
  if (!adapter->properties.modify)
    return error::unsupported;
  auto candidate = copy_candidate(source);
  return candidate ? adapter->properties.modify(*candidate, change) : candidate.error();
}
std::vector<guid> component_types() {
  std::vector<guid> output;
  for (const auto& adapter : adapters())
    output.push_back(adapter.type);
  return output;
}
guid component_description_type(guid type) noexcept {
  const auto* adapter = find(type, true);
  return adapter ? adapter->type : type;
}
error update_component(component_context context, const component_description& value) {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  if (!value)
    return error::invalid_argument;
  const auto* adapter = find(value->guid());
  if (!adapter)
    return error::unsupported;
  status = adapter->validate(value);
  if (status.failed())
    return status;
  auto current = adapter->capture(context.target);
  if (!current)
    return current.error();
  if (!*current)
    return error::not_found;
  return adapter->update ? adapter->update(context, value) : adapter->install(context, value);
}
error add_component(component_context context, const component_description& value, guid identity) {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  if (!value)
    return error::invalid_argument;
  const auto* adapter = find(value->guid());
  if (!adapter)
    return error::unsupported;
  auto current = adapter->capture(context.target);
  if (!current)
    return current.error();
  if (*current)
    return error::invalid_state;
  status = adapter->validate(value);
  if (status.failed())
    return status;
  auto& ids =
      instance_ids(*context.target.owner(), entity_id{context.target.entity().get()}).values;
  if (!identity)
    identity = value->uuid;
  if (!identity)
    return error::invalid_argument;
  // 先预留身份存储，安装失败不留下可观察的新组件。
  auto [entry, inserted] = ids.try_emplace(value->guid(), identity);
  status = adapter->install(context, value);
  if (status.ok())
    entry->second = identity;
  else if (inserted)
    ids.erase(entry);
  return status;
}
error remove_component(component_context context, guid type) {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  const auto* adapter = find(type);
  if (!adapter)
    return error::unsupported;
  auto current = adapter->capture(context.target);
  if (!current)
    return current.error();
  if (!*current)
    return error::not_found;
  status = adapter->remove(context.target);
  if (status.ok())
    instance_ids(*context.target.owner(), entity_id{context.target.entity().get()})
        .values.erase(type);
  return status;
}
error validate_components(const component_descriptions& values) {
  (void)adapters();
  for (const auto& value : values) {
    if (!value)
      return error::invalid_argument;
    const auto* adapter = find(value->guid());
    if (!adapter)
      return error::unsupported;
    auto status = adapter->validate(value);
    if (status.failed())
      return status;
  }
  return error::success;
}
result<component_descriptions> capture_components(world& owner, entity_id node) {
  auto target = owner.object(entity_id{node.get()});
  if (!target)
    return target.error();
  component_descriptions output;
  std::vector<component_description> entries;
  auto& ids = instance_ids(owner, node).values;
  for (const auto& adapter : adapters()) {
    auto value = adapter.capture(*target);
    if (!value)
      return value.error();
    if (*value) {
      auto found = ids.find(adapter.type);
      if (found == ids.end())
        found = ids.emplace(adapter.type, guid::generate()).first;
      auto identified = with_component_identity(**value, found->second);
      if (!identified)
        return identified.error();
      entries.push_back(std::move(*identified));
    } else
      ids.erase(adapter.type);
  }
  auto status = output.replace(entries);
  if (status.failed())
    return status;
  return output;
}
error replace_components(component_context context, const component_descriptions& values) {
  auto status = valid_target(context);
  if (status.ok())
    status = validate_components(values);
  if (status.failed())
    return status;
  auto current =
      capture_components(*context.target.owner(), entity_id{context.target.entity().get()});
  if (!current)
    return current.error();
  for (const auto& previous : *current) {
    const auto* next = values.find(previous->guid());
    if (next && (*next)->uuid == previous->uuid)
      continue;
    status = remove_component(context, previous->guid());
    if (status.failed())
      return status;
  }
  for (const auto& next : values) {
    const auto* previous = current->find(next->guid());
    const bool surviving = previous && (*previous)->uuid == next->uuid;
    if (surviving && equal_components({*previous}, {next}))
      continue;
    status = surviving ? update_component(context, next) : add_component(context, next, next->uuid);
    if (status.failed())
      return status;
  }
  return error::success;
}
bool equal_components(const component_descriptions& a, const component_descriptions& b) {
  if (a.size() != b.size())
    return false;
  for (const auto& value : a) {
    if (!value)
      return false;
    const auto* other = b.find(value->guid());
    const auto* adapter = find(value->guid());
    if (!other || !adapter || !adapter->equal(value, *other))
      return false;
  }
  return true;
}
result<std::vector<render_resource_source>>
component_dependencies(const component_descriptions& values) {
  auto status = validate_components(values);
  if (status.failed())
    return status;
  std::vector<render_resource_source> result;
  for (const auto& value : values) {
    const auto* adapter = find(value->guid());
    if (!adapter->dependencies)
      continue;
    auto next = adapter->dependencies(value);
    result.insert(result.end(), std::make_move_iterator(next.begin()),
                  std::make_move_iterator(next.end()));
  }
  return result;
}
error edit_component(component_descriptions& values, guid type, gneiss::property_id property,
                     const value& input) {
  const auto* adapter = find(type, true);
  if (!adapter)
    return error::unsupported;
  if (!adapter->properties.edit)
    return error::unsupported;
  const auto* source = values.find(adapter->type);
  if (!source)
    return error::not_found;
  auto candidate = copy_candidate(*source);
  if (!candidate)
    return candidate.error();
  auto edited = adapter->properties.edit(*candidate, type, property, input);
  if (!edited)
    return edited.error();
  values.set(std::move(*edited));
  return error::success;
}
result<value> read_runtime_component(world& owner, entity_id node, guid type,
                                     gneiss::property_id property) {
  auto target = owner.object(entity_id{node.get()});
  if (!target)
    return target.error();
  const auto* adapter = find(type, true);
  if (!adapter)
    return error::unsupported;
  if (!adapter->properties.read)
    return error::unsupported;
  auto captured = adapter->capture(*target);
  if (!captured)
    return captured.error();
  if (!*captured)
    return error::not_found;
  return adapter->properties.read(**captured, type, property);
}
error edit_runtime_component(component_context context, guid type, gneiss::property_id property,
                             const value& input) {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  const auto* adapter = find(type, true);
  if (!adapter)
    return error::unsupported;
  if (!adapter->properties.edit)
    return error::unsupported;
  auto current = adapter->capture(context.target);
  if (!current)
    return current.error();
  if (!*current)
    return error::not_found;
  auto reflected = meta::resolve(adapter->type);
  if (!reflected)
    return gneiss::error{gneiss::error::not_found};
  auto properties = reflected.template capability<gneiss::meta::cap::object>().properties();
  if (!reflected.registered())
    return error::not_found;
  for (const auto& field : properties) {
    if (field.id() == property && gneiss::is_edit_read_only(field))
      return error::unsupported;
  }
  auto candidate = copy_candidate(**current);
  if (!candidate)
    return candidate.error();
  auto edited = adapter->properties.edit(*candidate, type, property, input);
  return edited ? update_component(context, *edited) : edited.error();
}
}

namespace gneiss {
result<value> read_component_property(world& world, std::uint64_t entity, guid type,
                                      gneiss::property_id field) {
  if (type != math::transform::type_guid)
    return read_runtime_component(world, entity_id{entity}, type, field);
  auto target = world.object(entity_id{entity});
  if (!target)
    return target.error();
  auto object = target->local_transform();
  auto root = meta::view(std::as_const(object));
  auto bound = root.capability<meta::cap::object>();
  if (!bound)
    return bound.error();
  auto member = bound->member(field);
  return member ? member->read_value() : result<value>{member.error()};
}
error write_component_property(world& world, std::uint64_t entity, guid type,
                               gneiss::property_id field, const gneiss::value& value) {
  auto target = world.object(entity_id{entity});
  if (!target)
    return target.error();
  if (type != math::transform::type_guid)
    return edit_runtime_component({*target}, type, field, value);
  auto transform = target->local_transform();
  auto root = meta::view(transform);
  auto bound = root.capability<meta::cap::object>();
  if (!bound)
    return bound.error();
  auto member = bound->member(field);
  auto status = member ? member->assign(value) : member.error();
  return status.failed() ? status : target->set_local_transform(transform);
}
}

namespace gneiss {
template GNEISS_ENGINE_API camera_component* game_object::try_component<camera_component>() const;
template GNEISS_ENGINE_API mesh_component* game_object::try_component<mesh_component>() const;
template GNEISS_ENGINE_API error game_object::insert_component<camera_component>(camera_component) const;
template GNEISS_ENGINE_API error game_object::insert_component<mesh_component>(mesh_component) const;

template GNEISS_ENGINE_API error game_object::remove_component<camera_component>() const;
template GNEISS_ENGINE_API error game_object::remove_component<mesh_component>() const;
}

namespace gneiss {
template GNEISS_ENGINE_API error game_object::notify_component_modified<camera_component>() const;
template GNEISS_ENGINE_API error game_object::notify_component_modified<mesh_component>() const;
}
