// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_DETAIL_FRAME_PACKET_HPP_
#define GNEISS_RENDER_DETAIL_FRAME_PACKET_HPP_

#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/render/camera.hpp>
#include <gneiss/engine/function/render/debug_draw_list.hpp>
#include <gneiss/engine/function/render/frame_result.hpp>
#include <gneiss/engine/function/render/render_resource_data.hpp>
#include <gneiss/engine/function/render/ui_draw_list.hpp>
#include <gneiss/engine/platform/window/surface_state.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace gneiss::render {
class service;
class scene;
}
namespace gneiss::render::detail {

class resource_snapshot final {
public:
  /** 消费端按整帧去重后的资源集合准备镜像；借用不超过当前快照生命周期。 */
  [[nodiscard]] const auto& meshes() const noexcept { return meshes_; }
  [[nodiscard]] const auto& materials() const noexcept { return materials_; }
  [[nodiscard]] const mesh_data* get_mesh(rid mesh) const noexcept;
  /** 已捕获的预留材质返回中性缺省参数；未捕获或无效句柄仍返回空。 */
  [[nodiscard]] const material_data* get_material(rid material) const noexcept;
  [[nodiscard]] const texture_data* get_texture(rid texture) const noexcept;
  [[nodiscard]] std::shared_ptr<const mesh_data> share_mesh(rid handle) const noexcept {
    const auto found = meshes_.find(handle);
    return found == meshes_.end() ? nullptr : found->second;
  }
  [[nodiscard]] std::shared_ptr<const material_data> share_material(rid handle) const noexcept {
    const auto found = materials_.find(handle);
    return found == materials_.end() ? nullptr : found->second;
  }
  [[nodiscard]] std::shared_ptr<const texture_data> share_texture(rid handle) const noexcept {
    const auto found = textures_.find(handle);
    return found == textures_.end() ? nullptr : found->second;
  }
  [[nodiscard]] bool mesh_pending(rid mesh) const noexcept;
  [[nodiscard]] bool texture_pending(rid texture) const noexcept;

private:
  friend class render::scene;
  friend class render::service;

  std::unordered_map<rid, std::shared_ptr<const mesh_data>> meshes_;
  std::unordered_map<rid, std::shared_ptr<const material_data>> materials_;
  std::unordered_map<rid, std::shared_ptr<const texture_data>> textures_;
};

struct frame_capture_metrics final {
  float capture_ms{};
  std::size_t copied_payload_bytes{};
};

/** 绘制消费者的纯值输入，不持有上层资产或 ECS 对象。 */
struct draw_instance {
  rid mesh{};
  rid material{};
  math::matrix4 world_matrix{1.F};
  math::matrix4 normal_matrix{1.F};
  std::uint64_t layers{~UINT64_C(0)};
};
struct draw_scene {
  render_camera_snapshot camera{};
  bool has_camera{};
  std::uint64_t layers{~UINT64_C(0)};
  std::vector<draw_instance> instances;
};

struct draw_view {
  rid target{};
  std::uint32_t width{};
  std::uint32_t height{};
  draw_scene scene;
};

/** 已提交帧的自有数据；移动后不再借用主线程的逐帧可变内存。 */
struct frame_packet final {
  /** 由执行器在接受帧时写入，用于关联延迟返回的渲染结果。 */
  std::uint64_t sequence{};
  gneiss::window::surface_state surface;
  std::vector<draw_view> views;
  resource_snapshot resources;
  ui_draw_list ui;
  debug_draw_list debug;
  frame_capture_metrics capture;
  std::shared_ptr<gneiss::render::frame_image> readback;
};

} // namespace gneiss::render::detail

#endif
