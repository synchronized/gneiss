// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/detail/render_scene_state.hpp"
#include "engine/function/render/render_service.hpp"
#include <iostream>
#include <limits>
using namespace gneiss;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::cerr << __LINE__ << ": " #expression "\n";                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)
bool commands() {
  auto mesh = std::make_shared<mesh_data>();
  auto material = std::make_shared<material_data>();
  bool ready = true;
  auto pin = [&](const mesh_instance_description&) -> result<detail::scene_resource_pin> {
    if (!ready)
      return error::not_ready;
    return detail::scene_resource_pin{mesh, material, {}};
  };
  detail::render_scene_state state{65000, pin};
  detail::render_scene_state other{65001, pin};
  auto scene = state.create_scene();
  auto second = state.create_scene();
  CHECK(scene && second);
  const render::instance_key id{1};
  math::matrix4 model{1.F};
  model[1][0] = .5F;
  model[3][0] = 3.F;
  render::scene_commands batch{
      *scene,
      {render::create_instance{id, {.mesh = 1, .material = 2}}, render::set_transform{id, model}}};
  CHECK(!state.find(*scene, id));
  CHECK(!other.submit(std::move(batch)));
  CHECK(batch.entries.size() == 2);
  CHECK(state.submit(std::move(batch)));
  CHECK(batch.entries.empty() && batch.scene == null_rid);
  CHECK(!state.submit(std::move(batch)));
  CHECK(state.find(*scene, id)->description.world_matrix == model);
  render::scene_commands wrong_scene{*second, {render::destroy_instance{id}}};
  CHECK(!state.submit(std::move(wrong_scene)));
  {
    render::scene_commands abandoned{*scene,
                                     {render::create_instance{{2}, {.mesh = 1, .material = 2}},
                                      render::set_visibility{id, {.visible = false}}}};
  }
  CHECK(!state.find(*scene, {2}) && state.find(*scene, id)->description.visible);
  // 后一个无效命令使整个列表拒绝；已有状态、创建身份和调用方列表保持原样。
  render::scene_commands invalid{*scene,
                                 {render::set_visibility{id, {.visible = false}},
                                  render::create_instance{{2}, {.mesh = 1, .material = 2}},
                                  render::set_transform{id, math::matrix4{0.F}}}};
  CHECK(!state.submit(std::move(invalid)));
  CHECK(invalid.entries.size() == 3 && state.find(*scene, id)->description.visible);
  CHECK(!state.find(*scene, {2}));
  invalid.entries.pop_back();
  ready = false;
  auto pending = state.submit(std::move(invalid));
  CHECK(!pending && pending.error() == error::not_ready);
  CHECK(state.find(*scene, id)->description.visible);
  ready = true;
  CHECK(state.submit(std::move(invalid)));
  CHECK(!state.find(*scene, id)->description.visible && state.find(*scene, {2}));
  // 换绑失败不释放旧依赖；成功后新依赖存活，字段更新不覆盖矩阵。
  auto old_material = std::weak_ptr<material_data>{material};
  material = std::make_shared<material_data>();
  render::scene_commands rebind{*scene, {render::set_resources{id, 4, 5}}};
  ready = false;
  CHECK(!state.submit(std::move(rebind)));
  CHECK(state.find(*scene, id)->description.mesh == 1);
  ready = true;
  CHECK(state.submit(std::move(rebind)));
  CHECK(state.find(*scene, id)->resources.material == material);
  CHECK(state.find(*scene, id)->description.world_matrix == model);
  CHECK(!old_material.expired()); // 第二个实例仍持有旧材质。
  // 删除、同列表创建再删除与旧 key 重用。
  render::scene_commands deletion{*scene,
                                  {render::destroy_instance{{2}},
                                   render::create_instance{{3}, {.mesh = 1, .material = 2}},
                                   render::destroy_instance{{3}}}};
  CHECK(state.submit(std::move(deletion)));
  CHECK(!state.find(*scene, {2}) && !state.find(*scene, {3}));
  render::scene_commands stale{*scene, {render::destroy_instance{{2}}}};
  CHECK(!state.submit(std::move(stale)));
  render::scene_commands reuse{*scene, {render::create_instance{{3}, {.mesh = 1, .material = 2}}}};
  CHECK(!state.submit(std::move(reuse)));
  render::scene_commands zero{*scene, {render::create_instance{{}, {.mesh = 1, .material = 2}}}};
  CHECK(!state.submit(std::move(zero)));
  render::scene_commands duplicate{*scene,
                                   {render::destroy_instance{id}, render::destroy_instance{id}}};
  CHECK(!state.submit(std::move(duplicate)) && state.find(*scene, id));
  // 不同场景可以复用局部 key；最大 key 不允许回绕。
  render::scene_commands another{
      *second,
      {render::create_instance{id, {.mesh = 1, .material = 2}},
       render::create_instance{{std::numeric_limits<std::uint64_t>::max()},
                               {.mesh = 1, .material = 2}}}};
  CHECK(state.submit(std::move(another)));
  CHECK(state.find(*second, id));
  render::scene_commands wrapped{*second,
                                 {render::create_instance{{2}, {.mesh = 1, .material = 2}}}};
  CHECK(!state.submit(std::move(wrapped)));
  auto held = std::weak_ptr<mesh_data>{mesh};
  mesh.reset();
  CHECK(!held.expired());
  CHECK(state.destroy_scene(*scene));
  CHECK(!state.find(*scene, id) && state.find(*second, id));
  CHECK(!held.expired());
  CHECK(!state.destroy_scene(*scene));
  auto replacement = state.create_scene();
  CHECK(replacement && *replacement != *scene);
  CHECK(!state.submit(std::move(stale)));
  state.close();
  CHECK(held.expired());
  CHECK(!state.create_scene());
  CHECK(!state.submit(std::move(wrapped)));
  return true;
}
bool service_lifetime() {
  render::scene_commands pending;
  {
    render_service owner;
    render_service foreign;
    auto scene = owner.create_scene();
    CHECK(scene);
    auto mesh = owner.reserve_mesh();
    auto material = owner.reserve_material();
    CHECK(mesh && material);
    pending = {*scene, {render::create_instance{{1}, {.mesh = *mesh, .material = *material}}}};
    CHECK(!foreign.submit(std::move(pending)));
    auto not_ready = owner.submit(std::move(pending));
    CHECK(!not_ready && not_ready.error() == error::not_ready);
    CHECK(pending.entries.size() == 1);
    CHECK(owner.replace_mesh(*mesh, std::make_shared<mesh_data>()));
    CHECK(owner.replace_material(*material, std::make_shared<material_data>()));
    CHECK(owner.submit(std::move(pending)));
    // 无资源换绑的修改不重新查询已交还的外部 RID。
    CHECK(owner.destroy_mesh(*mesh).ok());
    CHECK(owner.destroy_material(*material).ok());
    pending = {*scene, {render::set_visibility{{1}, {.visible = false}}}};
    CHECK(owner.submit(std::move(pending)));
    pending = {*scene, {render::destroy_instance{{1}}}};
    owner.close_resources();
    CHECK(!owner.submit(std::move(pending)));
  }
  CHECK(pending.entries.size() == 1); // 命令可晚于服务销毁，不访问服务内存。
  return true;
}
int main() { return commands() && service_lifetime() ? 0 : 1; }
