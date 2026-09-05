// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_FRAME_PACKET_H_
#define GNEISS_RENDER_RENDER_FRAME_PACKET_H_

#include "platform/native_window_info.h"
#include "render/debug_draw_list.h"
#include "render/render_resource_service.h"
#include "render/ui_draw_list.h"
#include "world/render_snapshot.h"

#include <cstddef>
#include <unordered_map>

namespace gneiss::render_internal {

class render_resource_snapshot final {
public:
  [[nodiscard]] const mesh_resource* get_mesh(gneiss_mesh mesh) const noexcept;
  [[nodiscard]] const material_resource* get_material(gneiss_material material) const noexcept;
  [[nodiscard]] const texture_resource* get_texture(gneiss_texture texture) const noexcept;

private:
  friend gneiss_result capture_render_frame_packet(const application_internal::native_window_info&,
                                                   world_internal::render_snapshot,
                                                   const render_resource_service&,
                                                   const ui_draw_list&, const debug_draw_list&,
                                                   struct render_frame_packet&) noexcept;

  std::unordered_map<gneiss_mesh, mesh_resource> meshes_;
  std::unordered_map<gneiss_material, material_resource> materials_;
  std::unordered_map<gneiss_texture, texture_resource> textures_;
};

struct render_frame_capture_metrics final {
  float capture_ms{};
  std::size_t copied_payload_bytes{};
};

/** 已提交帧的自有数据；移动后不再借用主线程的逐帧可变内存。 */
struct render_frame_packet final {
  application_internal::native_window_info window;
  world_internal::render_snapshot scene;
  render_resource_snapshot resources;
  ui_draw_list ui;
  debug_draw_list debug;
  render_frame_capture_metrics capture;
};

[[nodiscard]] gneiss_result
capture_render_frame_packet(const application_internal::native_window_info& window,
                            world_internal::render_snapshot scene,
                            const render_resource_service& resources, const ui_draw_list& ui,
                            const debug_draw_list& debug, render_frame_packet& out_packet) noexcept;

} // namespace gneiss::render_internal

#endif
