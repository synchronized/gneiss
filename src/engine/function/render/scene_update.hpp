// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/export.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_scene.hpp"
#include <memory>
namespace gneiss {
namespace detail {
class render_scene_state;
}
/** 所属线程录制的单场景更新。未提交销毁时取消预留；不保活服务。
 * 成功提交后不可再次使用。首阶段只接受已就绪资源，未就绪返回 not_ready。 */
class scene_update final {
public:
  GNEISS_API ~scene_update();
  GNEISS_API scene_update(scene_update&&) noexcept;
  GNEISS_API scene_update& operator=(scene_update&&) noexcept;
  scene_update(const scene_update&) = delete;
  scene_update& operator=(const scene_update&) = delete;
  [[nodiscard]] GNEISS_API result<rid> create_mesh(const mesh_instance_description&);
  [[nodiscard]] GNEISS_API result<void> set_transform(rid, const math::matrix4&);
  [[nodiscard]] GNEISS_API result<void> set_mesh(rid, rid mesh);
  [[nodiscard]] GNEISS_API result<void> set_material(rid, rid material);
  [[nodiscard]] GNEISS_API result<void> set_layers(rid, std::uint64_t);
  [[nodiscard]] GNEISS_API result<void> set_visible(rid, bool);
  [[nodiscard]] GNEISS_API result<void> set_casts_shadows(rid, bool);
  [[nodiscard]] GNEISS_API result<void> destroy(rid);

private:
  friend class detail::render_scene_state;
  struct implementation;
  explicit scene_update(std::unique_ptr<implementation>);
  std::unique_ptr<implementation> impl_;
};
}
