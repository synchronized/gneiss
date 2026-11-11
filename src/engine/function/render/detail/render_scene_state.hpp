// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/render/scene_update.hpp"
#include <functional>
#include <unordered_map>
namespace gneiss::detail {
/** 首阶段固定资源 payload；后续资源身份 pin 将支持异步填充与重载。 */
struct scene_resource_pin {
  std::shared_ptr<const mesh_data> mesh;
  std::shared_ptr<const material_data> material;
  std::vector<std::shared_ptr<const texture_data>> textures;
};
class render_scene_state final : public std::enable_shared_from_this<render_scene_state> {
public:
  using pin_callback = std::function<result<scene_resource_pin>(const mesh_instance_description&)>;
  explicit render_scene_state(std::uint16_t domain, pin_callback pin)
      : scenes_(domain), instances_(domain), pin_(std::move(pin)) {}
  result<rid> create_scene();
  result<scene_update> begin_update(rid scene);
  result<void> submit(scene_update&& batch);
  result<void> destroy_scene(rid scene);
  void close() noexcept;
  struct instance {
    rid scene{};
    std::uint64_t reservation{};
    mesh_instance_description description;
    scene_resource_pin resources;
  };
  /** 只借用已提交实例；下一次结构修改后重新获取。 */
  const instance* find(rid handle) const noexcept;

private:
  friend class gneiss::scene_update;
  struct scene {
    std::vector<rid> instances;
  };
  rid_table<scene> scenes_;
  rid_table<instance> instances_;
  pin_callback pin_;
  std::uint64_t next_batch_{1};
  bool closed_{};
};
}
