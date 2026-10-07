// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene_update.hpp"

#include <array>
#include <iostream>

namespace {

gneiss::result run_example() {
  // user_data 必须比 Application 活得久，因此先声明状态，再声明拥有者。
  example::scene_state state;
  gneiss::application app;
  const gneiss::application_desc desc{
      .callbacks = {.user_data = &state, .update = example::update},
      .platform = gneiss::application_platform::callback,
  };
  if (const auto status = gneiss::application::create(desc, app); status.failed()) {
    return status;
  }

  gneiss::world_ref world;
  if (const auto status = app.get_world(world); status.failed()) {
    return status;
  }
  if (const auto status = world.create_entity(state.entity); status.failed()) {
    return status;
  }
  // 实体身份不自带变换；先把实体关联到场景节点。
  gneiss::scene_node_id node;
  if (const auto status = world.create_scene_node({}, state.entity, node); status.failed()) {
    return status;
  }
  const gneiss::transform initial{.translation = {1.0F, 2.0F, 3.0F}};
  if (const auto status = world.set_local_transform(state.entity, initial); status.failed()) {
    return status;
  }

  // 回调在第三帧请求退出；上限用于避免示例误改后无限运行。
  if (const auto status = app.run(8); status.failed()) {
    return status;
  }
  gneiss::transform final;
  if (const auto status = world.get_local_transform(state.entity, final); status.failed()) {
    return status;
  }
  if (state.updates != 3 || final.translation != std::array{4.0F, 2.0F, 3.0F}) {
    return gneiss::result::internal;
  }
  std::cout << "updates=" << state.updates << ", x=" << final.translation[0] << '\n';
  // 正常退出显式检查关闭结果；前面的失败路径仍由 RAII 收尾。
  return app.reset();
}

} // namespace

int main() try {
  const auto status = run_example();
  if (status.failed()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  return 0;
} catch (...) {
  // 控制台输出可能分配内存；资源已在栈展开时关闭。
  return 2;
}
