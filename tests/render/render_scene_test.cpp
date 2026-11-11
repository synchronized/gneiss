// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/detail/render_scene_state.hpp"
#include "engine/function/render/render_service.hpp"
#include <iostream>
#include <optional>
using namespace gneiss;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::cerr << __LINE__ << ": " #expression "\n";                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)
bool batches() {
  auto mesh = std::make_shared<mesh_data>();
  auto material = std::make_shared<material_data>();
  auto pin = [&](const mesh_instance_description&) -> result<detail::scene_resource_pin> {
    return detail::scene_resource_pin{mesh, material, {}};
  };
  auto state = std::make_shared<detail::render_scene_state>(65000, pin);
  auto other = std::make_shared<detail::render_scene_state>(65001, pin);
  auto scene = state->create_scene();
  CHECK(scene);
  auto second = state->create_scene();
  CHECK(second);
  auto batch = state->begin_update(*scene);
  CHECK(batch);
  auto id = batch->create_mesh({.mesh = 1, .material = 2});
  CHECK(id);
  CHECK(!state->find(*id));
  auto alien = state->begin_update(*scene);
  CHECK(alien);
  CHECK(!alien->set_visible(*id, false)); // 其他批次不可引用预留身份。
  math::matrix4 model{1.F};
  model[1][0] = .5F;
  model[3][0] = 3.F;
  CHECK(batch->set_transform(*id, model));
  CHECK(!other->submit(std::move(*batch)));
  CHECK(state->submit(std::move(*batch)));
  CHECK(state->find(*id) && state->find(*id)->description.world_matrix == model);
  CHECK(!state->submit(std::move(*batch)));
  auto wrong_scene = state->begin_update(*second);
  CHECK(wrong_scene);
  CHECK(!wrong_scene->destroy(*id));
  rid abandoned{};
  {
    auto cancelled = state->begin_update(*scene);
    CHECK(cancelled);
    auto reserved = cancelled->create_mesh({.mesh = 1, .material = 2});
    CHECK(reserved);
    abandoned = *reserved;
    CHECK(cancelled->set_visible(*id, false));
  }
  CHECK(state->find(*id)->description.visible);
  auto update = state->begin_update(*scene);
  CHECK(update);
  CHECK(!update->destroy(abandoned));
  CHECK(!update->set_transform(*id, math::matrix4{0.F}));
  CHECK(update->set_layers(*id, 4));
  auto newer = state->begin_update(*scene);
  CHECK(newer);
  CHECK(newer->set_visible(*id, false));
  CHECK(state->submit(std::move(*newer)));
  CHECK(state->submit(std::move(*update)));
  CHECK(!state->find(*id)->description.visible && state->find(*id)->description.layers == 4);
  // 无关对象删除导致旧批次失败时，其余修改不能部分生效。
  auto creation = state->begin_update(*scene);
  CHECK(creation);
  auto doomed = creation->create_mesh({.mesh = 1, .material = 2});
  CHECK(doomed);
  CHECK(state->submit(std::move(*creation)));
  auto stale = state->begin_update(*scene);
  CHECK(stale);
  CHECK(stale->set_visible(*id, true));
  CHECK(stale->destroy(*doomed));
  auto deletion = state->begin_update(*scene);
  CHECK(deletion);
  CHECK(deletion->destroy(*doomed));
  CHECK(!deletion->destroy(*doomed));
  CHECK(state->submit(std::move(*deletion)));
  CHECK(!state->submit(std::move(*stale)));
  CHECK(!state->find(*id)->description.visible);
  auto held = std::weak_ptr<mesh_data>{mesh};
  mesh.reset();
  CHECK(!held.expired());
  CHECK(state->destroy_scene(*scene));
  CHECK(!state->find(*id));
  CHECK(held.expired());
  CHECK(!state->destroy_scene(*scene));
  auto pending = state->begin_update(*second);
  CHECK(pending);
  state->close();
  CHECK(!pending->create_mesh({.mesh = 1, .material = 2}));
  CHECK(!state->create_scene());
  state.reset();
  CHECK(!pending->set_visible(*id, true));
  return true;
}
bool service_lifetime() {
  std::optional<scene_update> pending;
  {
    render_service owner;
    render_service foreign;
    auto scene = owner.create_scene();
    CHECK(scene);
    CHECK(!foreign.begin_update(*scene));
    auto batch = owner.begin_update(*scene);
    CHECK(batch);
    auto mesh = owner.reserve_mesh();
    auto material = owner.reserve_material();
    CHECK(mesh && material);
    auto not_ready = batch->create_mesh({.mesh = *mesh, .material = *material});
    CHECK(!not_ready && not_ready.error() == error::not_ready);
    auto foreign_material = foreign.reserve_material();
    CHECK(foreign_material);
    CHECK(!batch->create_mesh({.mesh = *mesh, .material = *foreign_material}));
    pending.emplace(std::move(*batch));
  }
  CHECK(!pending->create_mesh({.mesh = 1, .material = 2})); // 批次不访问已销毁的服务。
  return true;
}
int main() { return batches() && service_lifetime() ? 0 : 1; }
