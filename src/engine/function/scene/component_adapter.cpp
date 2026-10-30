// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/asset_uri.hpp"
#include <cmath>
#include <mutex>
#include <numbers>
namespace gneiss::engine {
namespace {
entity_id entity(world& owner, scene_node_id node) {
  entity_id id;
  (void)owner.node_get_entity(node, id);
  return id;
}
error camera_valid(const camera_description& value) {
  const auto& c = value.settings;
  return std::isfinite(c.fov) && std::isfinite(c.near) && std::isfinite(c.far) && c.fov > 0 &&
                 c.fov < std::numbers::pi_v<float> && c.near > 0 && c.far > c.near
             ? error::success
             : error::invalid_argument;
}
error camera_install(component_context& context, const camera_description& value) {
  return context.owner.entity_set_camera(entity(context.owner, context.node), value.settings,
                                         value.is_primary);
}
result<std::optional<camera_description>> camera_capture(world& owner, scene_node_id node) {
  math::camera_settings settings;
  const auto id = entity(owner, node);
  auto status = owner.entity_get_camera(id, settings);
  if (status == error::not_found)
    return std::optional<camera_description>{};
  if (status.failed())
    return status;
  entity_id active;
  status = owner.get_active_camera(active);
  if (status.failed() && status != error::not_ready)
    return status;
  return std::optional<camera_description>{{settings, status.ok() && active == id}};
}
error camera_edit(camera_description& value, guid type, meta::property_id property,
                  const engine::value& input) {
  if (type == camera_description::type_guid)
    return meta::write_property(type, property, &value, input);
  camera_properties properties;
  properties.settings = value.settings;
  auto status = meta::write_property(type, property, &properties, input);
  if (status.ok())
    value.settings = properties.settings;
  return status;
}
error mesh_valid(const mesh_renderer_description& value) {
  return validate_uri(value.mesh_uri).ok() && validate_uri(value.material_uri).ok()
             ? error::success
             : error::invalid_argument;
}
result<mesh_renderer> mesh_resolve(const mesh_renderer_description& value,
                                   resource_service& resources) {
  auto mesh = resources.find<mesh_resource>(value.mesh_uri);
  if (!mesh)
    return mesh.error();
  auto material = resources.find<material_resource>(value.material_uri);
  if (!material)
    return material.error();
  return mesh_renderer{*mesh, *material};
}
error mesh_install(component_context& context, const mesh_renderer_description& value) {
  auto ready = context.renderer;
  if (!ready.mesh || !ready.material)
    return error::not_ready;
  if (asset_uri(ready.mesh) != value.mesh_uri || asset_uri(ready.material) != value.material_uri) {
    if (!context.resources)
      return error::not_ready;
    if (asset_uri(ready.mesh) != value.mesh_uri) {
      auto resolved = context.resources->find<mesh_resource>(value.mesh_uri);
      if (!resolved)
        return resolved.error();
      ready.mesh = std::move(*resolved);
    }
    if (asset_uri(ready.material) != value.material_uri) {
      auto resolved = context.resources->find<material_resource>(value.material_uri);
      if (!resolved)
        return resolved.error();
      ready.material = std::move(*resolved);
    }
  }
  if (!ready.mesh || !ready.material)
    return error::not_ready;
  if (context.resources &&
      (!context.resources->owns(ready.mesh) || !context.resources->owns(ready.material)))
    return error::invalid_argument;
  return context.owner.entity_set_mesh_renderer(entity(context.owner, context.node), ready);
}
result<std::optional<mesh_renderer_description>> mesh_capture(world& owner, scene_node_id node) {
  auto value = owner.node_mesh_renderer(node);
  if (!value) {
    if (value.error() == error::not_found)
      return std::optional<mesh_renderer_description>{};
    return value.error();
  }
  if (asset_uri(value->mesh).empty() || asset_uri(value->material).empty())
    return error::invalid_state;
  return std::optional<mesh_renderer_description>{
      {std::string(asset_uri(value->mesh)), std::string(asset_uri(value->material))}};
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
         .remove = [](world& w, scene_node_id n) { return w.entity_remove_camera(entity(w, n)); },
         .equal =
             [](const camera_description& a, const camera_description& b) {
               return a.settings.fov == b.settings.fov && a.settings.near == b.settings.near &&
                      a.settings.far == b.settings.far && a.is_primary == b.is_primary;
             },
         .edit = camera_edit,
         .property_type = camera_component_type,
         .create_default = []() -> result<camera_description> {
           return camera_description{.settings = {std::numbers::pi_v<float> / 3, 0.1F, 1000.0F}};
         },
         .removable = true}));
    values.push_back(make_component_adapter<mesh_renderer_description>(
        {.validate = mesh_valid,
         .install = mesh_install,
         .capture = mesh_capture,
         .remove = [](world& w,
                      scene_node_id n) { return w.entity_remove_mesh_renderer(entity(w, n)); },
         .equal =
             [](const mesh_renderer_description& a, const mesh_renderer_description& b) {
               return a.mesh_uri == b.mesh_uri && a.material_uri == b.material_uri;
             },
         .dependencies =
             [](const mesh_renderer_description& value) {
               return std::vector<asset_source>{{value.mesh_uri, asset_type::mesh},
                                                {value.material_uri, asset_type::material}};
             },
         .removable = true}));
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
    if (adapter.type == type || (property && adapter.property_type == type))
      return &adapter;
  return nullptr;
}
error valid_target(component_context& context) {
  entity_id id;
  return context.owner.node_get_entity(context.node, id);
}
}
error register_scene_component(component_adapter adapter) noexcept try {
  if (!adapter.type || !adapter.property_type || !adapter.validate || !adapter.install ||
      !adapter.modify || !adapter.capture || !adapter.remove || !adapter.equal || !adapter.edit ||
      !adapter.dependencies)
    return error::invalid_argument;
  auto& state = registry();
  std::lock_guard lock(state.mutex);
  if (state.frozen)
    return error::invalid_state;
  for (const auto& old : state.values)
    if (old.type == adapter.type || old.property_type == adapter.property_type ||
        old.type == adapter.property_type || old.property_type == adapter.type)
      return error::invalid_argument;
  state.values.push_back(std::move(adapter));
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<component_description>
modify_component(const component_description& source,
                 const std::function<error(meta::mutable_instance)>& change) noexcept try {
  const auto* adapter = find(source.guid());
  if (!adapter)
    return error::unsupported;
  if (!change)
    return error::invalid_argument;
  return adapter->modify(source, change);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<std::vector<component_editor_info>> component_editor_types() noexcept try {
  std::vector<component_editor_info> output;
  for (const auto& adapter : adapters())
    output.push_back({adapter.type, bool(adapter.create_default), adapter.removable});
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<component_description> default_component_description(guid type) noexcept try {
  const auto* adapter = find(type);
  if (!adapter)
    return error::not_found;
  if (!adapter->create_default)
    return error::unsupported;
  auto value = adapter->create_default();
  if (!value)
    return value.error();
  if (value->guid() != type)
    return error::invalid_argument;
  auto status = adapter->validate(*value);
  return status.ok() ? std::move(value) : result<component_description>{status};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error add_component(component_context context, const component_description& value) noexcept try {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  const auto* adapter = find(value.guid());
  if (!adapter)
    return error::unsupported;
  auto current = adapter->capture(context.owner, context.node);
  if (!current)
    return current.error();
  if (*current)
    return error::invalid_state;
  status = adapter->validate(value);
  return status.ok() ? adapter->install(context, value) : status;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error remove_component(component_context context, guid type) noexcept try {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  const auto* adapter = find(type);
  if (!adapter || !adapter->removable)
    return error::unsupported;
  auto current = adapter->capture(context.owner, context.node);
  if (!current)
    return current.error();
  if (!*current)
    return error::not_found;
  return adapter->remove(context.owner, context.node);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error validate_components(const component_descriptions& values) noexcept try {
  (void)adapters();
  for (const auto& value : values) {
    const auto* adapter = find(value.guid());
    if (!adapter)
      return error::unsupported;
    auto status = adapter->validate(value);
    if (status.failed())
      return status;
  }
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<component_descriptions> capture_components(world& owner, scene_node_id node) noexcept try {
  component_context context{owner, node};
  auto status = valid_target(context);
  if (status.failed())
    return status;
  component_descriptions output;
  for (const auto& adapter : adapters()) {
    auto value = adapter.capture(owner, node);
    if (!value)
      return value.error();
    if (*value)
      output.set(std::move(**value));
  }
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error install_components(component_context context, const component_descriptions& values) noexcept
    try {
  auto status = valid_target(context);
  if (status.ok())
    status = validate_components(values);
  if (status.failed())
    return status;
  for (const auto& value : values) {
    status = find(value.guid())->install(context, value);
    if (status.failed())
      return status;
  }
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error replace_components(component_context context, const component_descriptions& values) noexcept
    try {
  auto status = valid_target(context);
  if (status.ok())
    status = validate_components(values);
  if (status.failed())
    return status;
  for (const auto& adapter : adapters()) {
    if (values.find(adapter.type))
      continue;
    status = adapter.remove(context.owner, context.node);
    if (status.failed() && status != error::not_found)
      return status;
  }
  return install_components(context, values);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
bool equal_components(const component_descriptions& a, const component_descriptions& b) {
  if (a.size() != b.size())
    return false;
  for (const auto& value : a) {
    const auto* other = b.find(value.guid());
    const auto* adapter = find(value.guid());
    if (!other || !adapter || !adapter->equal(value, *other))
      return false;
  }
  return true;
}
result<std::vector<asset_source>>
component_dependencies(const component_descriptions& values) noexcept try {
  auto status = validate_components(values);
  if (status.failed())
    return status;
  std::vector<asset_source> result;
  for (const auto& value : values) {
    auto next = find(value.guid())->dependencies(value);
    result.insert(result.end(), std::make_move_iterator(next.begin()),
                  std::make_move_iterator(next.end()));
  }
  return result;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error edit_component(component_descriptions& values, guid type, meta::property_id property,
                     const value& input) noexcept try {
  const auto* adapter = find(type, true);
  if (!adapter)
    return error::unsupported;
  const auto* source = values.find(adapter->type);
  if (!source)
    return error::not_found;
  auto edited = adapter->edit(*source, type, property, input);
  if (!edited)
    return edited.error();
  values.set(std::move(*edited));
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error edit_runtime_component(component_context context, guid type, meta::property_id property,
                             const value& input) noexcept try {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  const auto* adapter = find(type, true);
  if (!adapter)
    return error::unsupported;
  auto current = adapter->capture(context.owner, context.node);
  if (!current)
    return current.error();
  if (!*current)
    return error::not_found;
  // 属性编辑保留当前资源引用；只有 URI 被显式修改时才查找已就绪缓存。
  auto renderer = context.owner.node_mesh_renderer(context.node);
  if (renderer)
    context.renderer = std::move(*renderer);
  else if (renderer.error() != error::not_found)
    return renderer.error();
  auto edited = adapter->edit(**current, type, property, input);
  return edited ? adapter->install(context, *edited) : edited.error();
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<mesh_renderer> resolve_component_renderer(const component_descriptions& values,
                                                 resource_service& resources) noexcept try {
  const auto* renderer = values.get<mesh_renderer_description>();
  return renderer ? mesh_resolve(*renderer, resources) : result<mesh_renderer>{mesh_renderer{}};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
mesh_renderer
prepared_component_renderer(const component_descriptions& values,
                            const std::function<render_asset_ref(std::string_view)>& lookup) {
  const auto* renderer = values.get<mesh_renderer_description>();
  return renderer ? mesh_renderer{asset_cast<mesh_resource>(lookup(renderer->mesh_uri)),
                                  asset_cast<material_resource>(lookup(renderer->material_uri))}
                  : mesh_renderer{};
}
}
