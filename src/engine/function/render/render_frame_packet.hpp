// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_FRAME_PACKET_HPP_
#define GNEISS_RENDER_RENDER_FRAME_PACKET_HPP_

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/platform/native_window_info.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace gneiss::engine {
class render_service;

class render_resource_snapshot final {
public:
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
  friend error capture_render_frame_packet(const native_window_info&, render_snapshot,
                                           const render_service&, const ui_draw_list&,
                                           const debug_draw_list&,
                                           struct render_frame_packet&) noexcept;

  std::unordered_map<rid, std::shared_ptr<const mesh_data>> meshes_;
  std::unordered_map<rid, std::shared_ptr<const material_data>> materials_;
  std::unordered_map<rid, std::shared_ptr<const texture_data>> textures_;
};

struct render_frame_capture_metrics final {
  float capture_ms{};
  std::size_t copied_payload_bytes{};
};

/** 内部诊断回读：紧密 RGBA8，所有权属于请求；仅完成回执后可读取。 */
struct frame_image final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

/** 已提交帧的自有数据；移动后不再借用主线程的逐帧可变内存。 */
struct render_frame_packet final {
  /** 由执行器在接受帧时写入，用于关联延迟返回的渲染结果。 */
  std::uint64_t sequence{};
  native_window_info window;
  render_snapshot scene;
  render_resource_snapshot resources;
  ui_draw_list ui;
  debug_draw_list debug;
  render_frame_capture_metrics capture;
  std::shared_ptr<frame_image> readback;
};

[[nodiscard]] error
capture_render_frame_packet(const native_window_info& window, render_snapshot scene,
                            const render_service& resources, const ui_draw_list& ui,
                            const debug_draw_list& debug, render_frame_packet& out_packet) noexcept;

} // namespace gneiss::engine

#endif
