// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/components.hpp"
#include <unordered_set>
namespace gneiss::engine {
namespace {
/** 仅用于创建过程；移交场景前的失败路径回收整棵子树。 */
class scene_subtree_guard final {
public:
  scene_subtree_guard(world& owner, entity_id root) noexcept : owner_(owner), root_(root) {}
  ~scene_subtree_guard() {
    if (root_)
      (void)owner_.find(gneiss::entity_id{(root_).get()}).destroy();
  }
  scene_subtree_guard(const scene_subtree_guard&) = delete;
  scene_subtree_guard& operator=(const scene_subtree_guard&) = delete;
  void release() noexcept { root_ = {}; }

private:
  world& owner_;
  entity_id root_;
};

/** 旧网格创建入口的固定资源适配；不作为通用组件上下文的一部分。 */
inline auto fixed_renderer_resources(mesh_renderer renderer) {
  return [renderer = std::move(renderer)](
             const render_resource_source& source) -> result<render_resource_ref> {
    if (source.type == render_resource_kind::mesh && resource_uri(renderer.mesh) == source.uri)
      return render_resource_ref{renderer.mesh};
    if (source.type == render_resource_kind::material &&
        resource_uri(renderer.material) == source.uri)
      return render_resource_ref{renderer.material};
    return error::not_ready;
  };
}
}

result<scene_instantiation_result> instantiate(scene_instance destination,
                                               const composed_scene& composed,
                                               const scene_resource_bindings& assets,
                                               game_object parent) {
  if (!destination.owner())
    return error::invalid_handle;
  auto& world = *destination.owner();
  if (!world.is_owner_thread())
    return error::invalid_state;
  if (!destination.is_valid())
    return error::invalid_handle;
  if (parent.owner()) {
    if (parent.owner() != &world)
      return error::invalid_argument;
    auto valid = destination.contains(parent);
    if (!valid)
      return valid.error();
    if (!*valid)
      return error::invalid_handle;
  }
  std::map<object_address, const composed_object*> descriptions;
  for (const auto& object : composed.objects)
    if (!object.address.object || !descriptions.emplace(object.address, &object).second)
      return error::invalid_argument;
  for (const auto& object : composed.objects) {
    if (object.parent.source && !object.parent.object)
      return error::invalid_argument;
    for (const auto& component : object.effective.components)
      if (!component)
        return error::invalid_argument;
    auto ancestor = object.parent;
    std::size_t depth = 0;
    while (ancestor.object) {
      auto found = descriptions.find(ancestor);
      if (found == descriptions.end() || ++depth > descriptions.size())
        return error::invalid_argument;
      ancestor = found->second->parent;
    }
  }
  std::vector<game_object> created;
  created.reserve(composed.objects.size());
  struct creation_guard {
    std::vector<game_object>& objects;
    bool complete{};
    ~creation_guard() {
      if (complete)
        return;
      // 父对象可能先被销毁；后续失效子句柄无需再次删除。
      for (auto object : objects)
        if (object.is_valid())
          (void)object.destroy();
    }
  } guard{created};
  scene_instantiation_result receipt;
  receipt.instance = destination.id();
  receipt.node_count = composed.objects.size();
  receipt.objects.reserve(composed.objects.size());
  receipt.roots.reserve(composed.objects.size());
  std::map<object_address, game_object> objects;
  for (const auto& object : composed.objects) {
    // 来源地址只保存在回执中；运行对象以实体代次区分实例。
    auto made = destination.create({{}, object.effective.name}, object.effective.transform);
    if (!made)
      return made.error();
    created.push_back(*made);
    objects.emplace(object.address, *made);
    receipt.objects.push_back({object.address.object, object.address.source,
                               entity_id{made->entity().get()}, made->entity()});
  }
  // 对象全部存在后再连接层级，输入无需按父子排序。
  for (const auto& object : composed.objects) {
    auto target = objects.at(object.address);
    auto status = target.reparent(object.parent.object ? objects.at(object.parent) : parent);
    if (status.failed())
      return status;
    if (!object.parent.object)
      receipt.roots.push_back(target);
  }
  for (const auto& object : composed.objects) {
    component_context context{
        objects.at(object.address),
        [&](const render_resource_source& source) -> result<render_resource_ref> {
          auto found = assets.find(source.uri);
          return found == assets.end() ? result<render_resource_ref>{error::not_ready}
                                       : result<render_resource_ref>{found->second};
        }};
    for (const auto& component : object.effective.components) {
      auto status = add_component(context, component, component->uuid);
      if (status.failed())
        return status;
    }
  }
  // 第二遍仅解析引用；目标对象和组件已全部安装，支持前向引用。
  for (const auto& object : composed.objects) {
    component_context context{objects.at(object.address)};
    context.lookup_object = [&](object_address address) -> result<game_object> {
      auto found = objects.find(address);
      return found == objects.end() ? result<game_object>{error::not_found}
                                    : result<game_object>{found->second};
    };
    auto status = resolve_component_references(context, object.effective.components);
    if (status.failed())
      return status;
  }
  guard.complete = true;
  return receipt;
}

namespace {
error validate(world& owner, scene_instance_id scene, entity_id node) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  auto instance = scene_instance::borrow(owner, scene);
  if (!instance)
    return instance.error();
  auto object = owner.object(entity_id{node.get()});
  if (!object)
    return object.error();
  auto member = instance->contains(*object);
  if (!member)
    return member.error();
  return *member ? error::success : error::invalid_handle;
}
}
result<entity_id> create_mesh_renderer_node(world& owner, asset_manager& assets,
                                            scene_instance_id instance,
                                            const mesh_renderer_node_creation& desc,
                                            const scene_mesh_binding* prepared) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  auto destination = scene_instance::borrow(owner, instance);
  if (!destination)
    return destination.error();
  scene_mesh_binding binding;
  if (prepared) {
    if (!assets.owns(prepared->mesh) || !assets.owns(prepared->material) ||
        resource_uri(prepared->mesh) != desc.mesh_uri ||
        resource_uri(prepared->material) != desc.material_uri)
      return error::invalid_argument;
    binding = *prepared;
  } else {
    auto mesh = assets.find<engine::mesh>(desc.mesh_uri);
    if (!mesh)
      return mesh.error();
    auto material = assets.find<engine::material>(desc.material_uri);
    if (!material)
      return material.error();
    binding = {*mesh, *material};
  }
  auto parent =
      desc.parent ? owner.object(entity_id{desc.parent.get()}) : result<game_object>{game_object{}};
  if (!parent)
    return parent.error();
  auto object = destination->create({desc.uuid, std::string(desc.name)}, {}, *parent);
  if (!object)
    return object.error();
  result<entity_id> created{entity_id{object->entity().get()}};
  if (!created)
    return created.error();
  scene_subtree_guard guard(owner, *created);
  auto description = make_component_description(
      mesh_renderer_description{std::string(desc.mesh_uri), std::string(desc.material_uri)});
  auto target = owner.object(entity_id{created->get()});
  if (!target)
    return target.error();
  auto status =
      add_component({*target, fixed_renderer_resources({binding.mesh, binding.material}), &assets},
                    description, desc.component_uuid);
  if (status.failed())
    return status;
  guard.release();
  return *created;
}
error set_mesh_renderer(world& owner, asset_manager& assets, scene_instance_id scene,
                        entity_id node, std::string_view mesh_uri, std::string_view material_uri) {
  auto status = validate(owner, scene, node);
  if (status.failed())
    return status;
  auto current = owner.find(gneiss::entity_id{(node).get()})
                     .read_component<gneiss::engine::mesh_renderer_component>();
  if (!current && current.error() != error::not_found)
    return current.error();
  auto binding = current ? current->value : mesh_renderer{};
  if (resource_uri(binding.mesh) != mesh_uri) {
    if (mesh_uri.empty())
      binding.mesh = {};
    else {
      auto loaded = assets.find<engine::mesh>(mesh_uri);
      if (!loaded)
        return loaded.error();
      binding.mesh = *loaded;
    }
  }
  if (resource_uri(binding.material) != material_uri) {
    if (material_uri.empty())
      binding.material = {};
    else {
      auto loaded = assets.find<engine::material>(material_uri);
      if (!loaded)
        return loaded.error();
      binding.material = *loaded;
    }
  }
  return owner.find(entity_id{node.get()})
      .set_component(gneiss::engine::mesh_renderer_component{binding});
}
result<scene_mesh_binding> capture_mesh_binding(world& owner, scene_instance_id scene,
                                                entity_id node) {
  auto status = validate(owner, scene, node);
  if (status.failed())
    return status;
  auto current = owner.find(gneiss::entity_id{(node).get()})
                     .read_component<gneiss::engine::mesh_renderer_component>();
  if (!current)
    return current.error() == error::not_found ? result<scene_mesh_binding>{scene_mesh_binding{}}
                                               : result<scene_mesh_binding>{current.error()};
  return scene_mesh_binding{current->value.mesh, current->value.material};
}
error replace_mesh_binding(world& owner, scene_instance_id scene, entity_id node,
                           const scene_mesh_binding& expected,
                           const scene_mesh_binding& replacement, guid component_uuid,
                           asset_manager* resources) {
  auto current = capture_mesh_binding(owner, scene, node);
  if (!current)
    return current.error();
  if (resources && ((replacement.mesh && !resources->owns(replacement.mesh)) ||
                    (replacement.material && !resources->owns(replacement.material))))
    return error::invalid_argument;
  if (current->mesh != expected.mesh || current->material != expected.material)
    return error::invalid_state;
  auto existing = owner.find(gneiss::entity_id{(node).get()})
                      .read_component<gneiss::engine::mesh_renderer_component>();
  if (!existing && existing.error() != error::not_found)
    return existing.error();
  if (!existing) {
    auto target = owner.object(entity_id{node.get()});
    if (!target)
      return target.error();
    return add_component(
        {*target, fixed_renderer_resources({replacement.mesh, replacement.material}), resources},
        make_component_description(
            mesh_renderer_description{std::string(resource_uri(replacement.mesh)),
                                      std::string(resource_uri(replacement.material))}),
        component_uuid);
  }
  const mesh_renderer_replacement change{entity_id{node.get()},
                                         {expected.mesh, expected.material},
                                         {replacement.mesh, replacement.material}};
  return replace_mesh_renderers(owner, {&change, 1});
}
}

namespace gneiss::engine {
error replace_mesh_renderers(world& owner, std::span<const mesh_renderer_replacement> changes) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  std::unordered_set<std::uint64_t> entities;
  for (const auto& change : changes) {
    if (!entities.insert(change.entity.get()).second)
      return error::invalid_argument;
    auto current = owner.find(change.entity).read_component<mesh_renderer_component>();
    if (!current)
      return current.error() == error::not_found ? error::invalid_state : current.error();
    if (current->value.mesh != change.expected.mesh ||
        current->value.material != change.expected.material)
      return error::invalid_state;
  }
  for (const auto& change : changes) {
    auto status =
        owner.find(change.entity).set_component(mesh_renderer_component{change.replacement});
    if (status.failed())
      return status;
  }
  return error::success;
}
}
