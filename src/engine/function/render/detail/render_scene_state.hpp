// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/rid_table.hpp"
#include "engine/function/render/commands.hpp"
#include "engine/function/render/detail/resource_identity.hpp"
#include <functional>
#include <map>
namespace gneiss::detail {
class render_scene_state final {
public:
  using pin_callback = std::function<result<scene_resource_pin>(const mesh_instance_description&)>;
  explicit render_scene_state(std::uint16_t domain, pin_callback pin)
      : scenes_(domain), pin_(std::move(pin)) {}
  result<rid> create_scene();
  result<void> submit(render::scene_commands&& commands);
  result<void> destroy_scene(rid scene);
  void close() noexcept;
  struct instance {
    mesh_instance_description description;
    scene_resource_pin resources;
  };
  /** 只借用已接受实例；下一次提交或场景销毁后重新获取。 */
  const instance* find(rid scene, render::instance_key key) const noexcept;

private:
  struct scene {
    std::map<std::uint64_t, instance> instances;
    std::uint64_t last_key{};
  };
  rid_table<scene> scenes_;
  pin_callback pin_;
  bool closed_{};
};
}
