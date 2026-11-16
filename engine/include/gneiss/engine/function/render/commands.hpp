// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/function/render/render_scene.hpp>
#include <variant>
#include <vector>

namespace gneiss::render {
/** 场景内实例身份；由单一生产者递增分配，零无效，同场景生命周期内不复用。 */
struct instance_key {
  std::uint64_t value{};
  explicit constexpr operator bool() const noexcept { return value != 0; }
  friend constexpr bool operator==(instance_key, instance_key) = default;
};
struct instance_visibility {
  std::uint64_t layers{~UINT64_C(0)};
  bool visible{true};
  bool casts_shadows{true};
};
struct create_instance {
  instance_key id;
  mesh_instance_description description;
};
struct set_transform {
  instance_key id;
  math::matrix4 world_matrix{1.F};
};
struct set_resources {
  instance_key id;
  rid mesh{};
  rid material{};
};
struct set_visibility {
  instance_key id;
  instance_visibility visibility;
};
struct destroy_instance {
  instance_key id;
};
using command =
    std::variant<create_instance, set_transform, set_resources, set_visibility, destroy_instance>;
/** 自有命令数据；不保活服务或资源，不预留后端对象，析构无服务副作用。
 * 提交前生产者须保活命令引用的资源。创建 key 按递增顺序提交。
 * 所属线程提交成功后清空；返回失败时列表和场景均不改变。 */
struct scene_commands {
  rid scene{};
  std::vector<command> entries;
};
}
