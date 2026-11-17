// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/asset/detail/asset_construction.hpp"
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include "function/scene/rendering/world_render_binding.hpp"
#include <gneiss/engine/function/scene/world.hpp>
#include <cstdio>
#include <thread>

struct observer {
  gneiss::object_changes objects;
  int batches{};
  void receive(const gneiss::object_changes& changes) {
    ++batches;
    for (const auto& [id, change] : changes)
      objects.insert_or_assign(id, change);
  }
};
int main() {
  using namespace gneiss;
  const auto fail = [](const char* behavior) {
    std::fprintf(stderr, "World 增量同步失败：%s\n", behavior);
    return 1;
  };
  if (initialize_meta().failed())
    return fail("初始化类型注册");
  gneiss::render::service render;
  detail::asset_construction assets{render};
  auto mesh = assets.reserve_mesh();
  auto material = assets.reserve_material();
  auto owner = create_native();
  if (!mesh || !material || !owner)
    return fail("创建资源与 World");
  auto scene = (*owner)->create_scene();
  if (!scene)
    return fail("创建场景");
  auto root = scene->create_object();
  auto child = root ? scene->create_object({}, *root) : result<game_object>{root.error()};
  if (!child || child->add_component<mesh_component>(mesh_component{*mesh, *material}).failed())
    return fail("创建带网格的子对象");
  observer first, second;
  events::scoped_connection first_subscription =
      (*owner)->changes().connect<&observer::receive>(first);
  events::scoped_connection second_subscription =
      (*owner)->changes().connect<&observer::receive>(second);
  auto binding = world_render_binding::create(**owner, render);
  auto other_binding = world_render_binding::create(**owner, render);
  if (!binding || !other_binding || !(*binding)->synchronize() || !(*other_binding)->synchronize())
    return fail("建立两个绑定并首次同步");
  math::transform transform;
  transform.translation.x = 5;
  if (root->set_local_transform(transform).failed() ||
      root->set_local_transform(transform).failed() || !first.objects.empty())
    return fail("父变换传播至子对象");
  (*owner)->flush_changes();
  if (!first.objects.at(child->entity()).transform ||
      !second.objects.at(child->entity()).transform || first.batches != 1 || second.batches != 1)
    return fail("父变换传播至子对象");
  // 重复修改合并；一个消费者确认不影响另一个。
  first.objects.clear();
  if (!(*binding)->synchronize() || !(*other_binding)->synchronize() || second.objects.empty())
    return fail("消费者独立确认");
  bool called = false;
  if (child->modify_component<camera_component>([&](auto&) noexcept { called = true; }) !=
          error::not_found || called ||
      child->notify_component_modified<camera_component>() != error::not_found)
    return fail("缺失组件不得调用修改回调");
  const auto type = runtime_type_of<mesh_component>();
  if (child->remove_component<mesh_component>().failed() ||
      child->add_component<mesh_component>(mesh_component{*mesh, *material}).failed())
    return fail("移除后重新添加保留身份失效信息");
  (*owner)->flush_changes();
  if (!first.objects.at(child->entity()).components.at(type) || !(*binding)->synchronize())
    return fail("移除后重新添加保留身份失效信息");
  first.objects.clear();
  second.objects.clear();
  if (child->reparent({}).failed())
    return fail("改父节点同步");
  (*owner)->flush_changes();
  if (!first.objects.at(child->entity()).transform || !(*binding)->synchronize())
    return fail("改父节点同步");
  // 失效资源使提交失败；修复后不重复创建已接受实例，也不丢弃变化。
  auto invalid_mesh = assets.reserve_mesh();
  if (!invalid_mesh || render.destroy_mesh(resource_handle(*invalid_mesh)).failed())
    return fail("制造失效资源");
  if (child->modify_component<mesh_component>(
          [&](auto& component) noexcept { component.mesh = *invalid_mesh; }).failed())
    return fail("失效资源提交拒绝");
  (*owner)->flush_changes();
  if ((*binding)->synchronize())
    return fail("失效资源提交拒绝");
  child->try_component<mesh_component>()->mesh = *mesh;
  (*owner)->flush_changes();
  if (!(*binding)->synchronize())
    return fail("无新通知时使用保留待办重试");
  error wrong_thread;
  std::thread worker([&] {
    auto result = (*binding)->synchronize();
    wrong_thread = result ? error{error::success} : result.error();
  });
  worker.join();
  if (wrong_thread != error::invalid_state)
    return fail("拒绝其他线程同步");
  const auto old_id = child->entity();
  child->destroy();
  (*owner)->flush_changes();
  if (!first.objects.at(old_id).destroyed || !(*binding)->synchronize() ||
      !(*other_binding)->synchronize())
    return fail("删除对象与多绑定同步");
  auto replacement = scene->create_object();
  if (!replacement || replacement->entity() == old_id ||
      replacement->add_component<mesh_component>(mesh_component{*mesh, *material}).failed())
    return fail("实体槽位复用后新身份");
  (*owner)->flush_changes();
  if (!(*binding)->synchronize())
    return fail("实体槽位复用后新身份");
  // 提交前创建又删除，不能留下渲染实例；卸载同时处理已有成员。
  auto temporary = scene->create_object();
  if (!temporary ||
      temporary->add_component<mesh_component>(mesh_component{*mesh, *material}).failed())
    return fail("未提交对象删除及场景卸载");
  temporary->destroy();
  scene->unload();
  (*owner)->flush_changes();
  if (!(*binding)->synchronize())
    return fail("未提交对象删除及场景卸载");
  const auto render_scene = (*binding)->scene();
  binding->reset();
  if (render.destroy_scene(render_scene))
    return fail("绑定析构释放 Render 场景");
  return 0;
}
