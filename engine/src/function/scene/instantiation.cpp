// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
namespace gneiss {
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
    if (!destination.contains(parent))
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
  receipt.instance = destination;
  receipt.node_count = composed.objects.size();
  receipt.objects.reserve(composed.objects.size());
  receipt.roots.reserve(composed.objects.size());
  std::map<object_address, game_object> objects;
  for (const auto& object : composed.objects) {
    // 来源地址只保存在回执中；运行对象以实体代次区分实例。
    auto made = destination.create_object({object.effective.name, object.effective.transform});
    if (!made)
      return made.error();
    created.push_back(*made);
    objects.emplace(object.address, *made);
    receipt.objects.push_back({object.address.object, object.address.source, made->entity()});
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

}
