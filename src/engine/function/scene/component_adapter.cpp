// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/component_adapter.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/resource/asset_uri.hpp"
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
namespace gneiss::engine {
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
entity_id entity(world& owner, scene_node_id node) {
  entity_id id;
  (void)owner.node_get_entity(node, id);
  return id;
}
// 与实体同寿命的身份元数据，不持有编辑内容或历史。
struct component_instance_ids {
  std::map<guid, guid> values;
};
component_instance_ids& instance_ids(world& owner, scene_node_id node) {
  auto state = detail::world_component_access::get(owner);
  if (!state)
    throw std::invalid_argument("无效 World");
  auto id = entity(owner, node).get();
  auto* identities = (*state)->get<component_instance_ids>(id);
  return identities ? *identities : (*state)->emplace<component_instance_ids>(id);
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
error mesh_valid(const mesh_renderer_description& value) {
  return (value.mesh_uri.empty() || validate_uri(value.mesh_uri).ok()) &&
                 (value.material_uri.empty() || validate_uri(value.material_uri).ok())
             ? error::success
             : error::invalid_argument;
}
result<mesh_renderer> mesh_resolve(const mesh_renderer_description& value,
                                   asset_manager& resources) {
  mesh_renderer result;
  if (!value.mesh_uri.empty()) {
    auto loaded = resources.load({value.mesh_uri, render_resource_kind::mesh});
    if (!loaded)
      return loaded.error();
    result.mesh = loaded->get<engine::mesh>();
  }
  if (!value.material_uri.empty()) {
    auto loaded = resources.load({value.material_uri, render_resource_kind::material});
    if (!loaded)
      return loaded.error();
    result.material = loaded->get<engine::material>();
  }
  return result;
}
error mesh_install(component_context& context, const mesh_renderer_description& value) {
  auto ready = context.renderer;
  if (value.mesh_uri.empty())
    ready.mesh = {};
  else if (resource_uri(ready.mesh) != value.mesh_uri) {
    if (!context.resources)
      return error::not_ready;
    auto loaded = context.resources->load({value.mesh_uri, render_resource_kind::mesh});
    if (!loaded)
      return loaded.error();
    ready.mesh = loaded->get<engine::mesh>();
  }
  if (value.material_uri.empty())
    ready.material = {};
  else if (resource_uri(ready.material) != value.material_uri) {
    if (!context.resources)
      return error::not_ready;
    auto loaded = context.resources->load({value.material_uri, render_resource_kind::material});
    if (!loaded)
      return loaded.error();
    ready.material = loaded->get<engine::material>();
  }
  if (context.resources && ((ready.mesh && !context.resources->owns(ready.mesh)) ||
                            (ready.material && !context.resources->owns(ready.material))))
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
  return std::optional<mesh_renderer_description>{
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
         .remove = [](world& w, scene_node_id n) { return w.entity_remove_camera(entity(w, n)); },
         .equal =
             [](const camera_description& a, const camera_description& b) {
               return a.settings.fov == b.settings.fov && a.settings.near == b.settings.near &&
                      a.settings.far == b.settings.far && a.is_primary == b.is_primary;
             },
         .create_default = []() -> result<camera_description> {
           return camera_description{{std::numbers::pi_v<float> / 3, 0.1F, 1000.0F}};
         },
         .removable = true,
         .prepare_editor = [](world& owner, scene_node_id node,
                              const camera_description& value) -> result<camera_description> {
           auto current = camera_capture(owner, node);
           if (!current)
             return current.error();
           if (!*current)
             return error::not_found;
           auto next = value;
           next.is_primary = (**current).is_primary;
           return next;
         }}));
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
               std::vector<render_resource_source> result;
               if (!value.mesh_uri.empty())
                 result.push_back({value.mesh_uri, render_resource_kind::mesh});
               if (!value.material_uri.empty())
                 result.push_back({value.material_uri, render_resource_kind::material});
               return result;
             },
         .removable = true,
         .editor_route = component_editor_route::mesh_assets}));
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
      !adapter.read || !adapter.modify || !adapter.capture || !adapter.remove || !adapter.equal ||
      !adapter.edit || !adapter.dependencies)
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
                 const std::function<error(meta::instance)>& change) noexcept try {
  if (!source)
    return error::invalid_argument;
  const auto* adapter = find(source->guid());
  if (!adapter)
    return error::unsupported;
  if (!change)
    return error::invalid_argument;
  auto candidate = copy_candidate(source);
  return candidate ? adapter->modify(*candidate, change) : candidate.error();
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<std::vector<component_editor_info>> component_editor_types() noexcept try {
  std::vector<component_editor_info> output;
  for (const auto& adapter : adapters())
    output.push_back(
        {adapter.type, bool(adapter.create_default), adapter.removable, adapter.editor_route});
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
guid component_description_type(guid type) noexcept {
  const auto* adapter = find(type, true);
  return adapter ? adapter->type : type;
}
result<component_editor_info> component_editor(guid type) noexcept {
  const auto* adapter = find(type);
  if (!adapter)
    return error::not_found;
  return component_editor_info{adapter->type, bool(adapter->create_default), adapter->removable,
                               adapter->editor_route};
}
result<component_description>
prepare_component_editor_value(world& owner, scene_node_id node,
                               const component_description& value) noexcept try {
  if (!value)
    return error::invalid_argument;
  const auto* adapter = find(value->guid());
  if (!adapter)
    return error::not_found;
  auto prepared = adapter->prepare_editor ? adapter->prepare_editor(owner, node, value)
                                          : result<component_description>{value};
  if (!prepared)
    return prepared.error();
  auto status = adapter->validate(*prepared);
  return status.ok() ? std::move(prepared) : result<component_description>{status};
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
  if (!*value || (*value)->guid() != type)
    return error::invalid_argument;
  auto status = adapter->validate(*value);
  return status.ok() ? std::move(value) : result<component_description>{status};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error update_component(component_context context, const component_description& value) noexcept try {
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
  auto current = adapter->capture(context.owner, context.node);
  if (!current)
    return current.error();
  if (!*current)
    return error::not_found;
  return adapter->update ? adapter->update(context, value) : adapter->install(context, value);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error add_component(component_context context, const component_description& value,
                    guid identity) noexcept try {
  auto status = valid_target(context);
  if (status.failed())
    return status;
  if (!value)
    return error::invalid_argument;
  const auto* adapter = find(value->guid());
  if (!adapter)
    return error::unsupported;
  auto current = adapter->capture(context.owner, context.node);
  if (!current)
    return current.error();
  if (*current)
    return error::invalid_state;
  status = adapter->validate(value);
  if (status.failed())
    return status;
  auto& ids = instance_ids(context.owner, context.node).values;
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
  status = adapter->remove(context.owner, context.node);
  if (status.ok())
    instance_ids(context.owner, context.node).values.erase(type);
  return status;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error validate_components(const component_descriptions& values) noexcept try {
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
  std::vector<component_description> entries;
  auto& ids = instance_ids(owner, node).values;
  for (const auto& adapter : adapters()) {
    auto value = adapter.capture(owner, node);
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
  status = output.replace(entries);
  if (status.failed())
    return status;
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
  auto& ids = instance_ids(context.owner, context.node).values;
  for (const auto& value : values) {
    auto [entry, inserted] = ids.try_emplace(value->guid(), values.identity(value->guid()));
    status = find(value->guid())->install(context, value);
    if (status.failed() && inserted)
      ids.erase(entry);
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
  auto current = capture_components(context.owner, context.node);
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
    status = surviving ? update_component(context, next) : install_components(context, {next});
    if (status.failed())
      return status;
  }
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
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
component_dependencies(const component_descriptions& values) noexcept try {
  auto status = validate_components(values);
  if (status.failed())
    return status;
  std::vector<render_resource_source> result;
  for (const auto& value : values) {
    auto next = find(value->guid())->dependencies(value);
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
  auto candidate = copy_candidate(*source);
  if (!candidate)
    return candidate.error();
  auto edited = adapter->edit(*candidate, type, property, input);
  if (!edited)
    return edited.error();
  values.set(std::move(*edited));
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<value> read_runtime_component(world& owner, scene_node_id node, guid type,
                                     meta::property_id property) noexcept try {
  component_context context{owner, node};
  auto status = valid_target(context);
  if (status.failed())
    return status;
  const auto* adapter = find(type, true);
  if (!adapter)
    return error::unsupported;
  auto captured = adapter->capture(owner, node);
  if (!captured)
    return captured.error();
  if (!*captured)
    return error::not_found;
  return adapter->read(**captured, type, property);
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
  auto reflected = meta::describe_type(adapter->type);
  if (!reflected)
    return reflected.error();
  for (const auto& field : reflected->properties) {
    if (field.id == property &&
        meta::has_flags(field.flags, meta::property_flags::editor_read_only))
      return error::unsupported;
  }
  auto candidate = copy_candidate(**current);
  if (!candidate)
    return candidate.error();
  auto edited = adapter->edit(*candidate, type, property, input);
  return edited ? adapter->install(context, *edited) : edited.error();
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<mesh_renderer> resolve_component_renderer(const component_descriptions& values,
                                                 asset_manager& resources) noexcept try {
  const auto* renderer = values.get<mesh_renderer_description>();
  return renderer ? mesh_resolve(*renderer, resources) : result<mesh_renderer>{mesh_renderer{}};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
mesh_renderer
prepared_component_renderer(const component_descriptions& values,
                            const std::function<render_resource_ref(std::string_view)>& lookup) {
  const auto* renderer = values.get<mesh_renderer_description>();
  return renderer
             ? mesh_renderer{renderer->mesh_uri.empty()
                                 ? ref<const engine::mesh>{}
                                 : resource_cast<engine::mesh>(lookup(renderer->mesh_uri)),
                             renderer->material_uri.empty()
                                 ? ref<const engine::material>{}
                                 : resource_cast<engine::material>(lookup(renderer->material_uri))}
             : mesh_renderer{};
}
}

namespace gneiss::engine {
namespace {
/** World 接口负责线程与实体校验；反射只修改副本，成功后才提交。 */
template <class Access>
error access_component(world& world, std::uint64_t entity, guid type, bool write,
                       Access&& access) noexcept {
  if (type == transform_component_type) {
    math::transform value;
    auto operation = world.entity_get_local_transform(entity_id{entity}, value);
    if (operation.failed())
      return operation;
    operation = access(&value);
    return operation.failed() || !write
               ? operation
               : world.entity_set_local_transform(entity_id{entity}, value);
  }
  return error::unsupported;
}
}
result<value> read_component_property(world& world, std::uint64_t entity, guid type,
                                      meta::property_id field) noexcept {
  if (type != transform_component_type) {
    auto node = world.entity_node(entity_id{entity});
    return node ? read_runtime_component(world, *node, type, field) : result<value>{node.error()};
  }
  math::transform object;
  auto status = world.entity_get_local_transform(entity_id{entity}, object);
  if (status.failed())
    return status;
  auto root = meta::view(std::as_const(object));
  if (!root)
    return root.error();
  auto member = root->member(field);
  return member ? member->read_value() : result<value>{member.error()};
}
error write_component_property(world& world, std::uint64_t entity, guid type,
                               meta::property_id field, const engine::value& value) noexcept {
  if (type != transform_component_type) {
    auto node = world.entity_node(entity_id{entity});
    return node ? edit_runtime_component({world, *node}, type, field, value) : node.error();
  }
  return access_component(world, entity, type, true, [&](auto* object) {
    auto root = meta::view(*object);
    if (!root)
      return root.error();
    auto member = root->member(field);
    return member ? member->assign(value) : member.error();
  });
}
}
