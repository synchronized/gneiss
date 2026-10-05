// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_HPP_
#define GNEISS_APPLICATION_HPP_

#include <gneiss/application.h>
#include <gneiss/core/result.hpp>
#include <gneiss/input.hpp>
#include <gneiss/render.hpp>
#include <gneiss/world.hpp>

#include <cstdint>
#include <exception>
#include <utility>

namespace gneiss {

/** 独占拥有 Application 的 RAII 包装；只允许在创建线程访问。
 * 析构或移动覆盖若关闭失败则终止进程；需处理错误时先显式 reset()。 */
class application final {
public:
  application() noexcept = default;
  ~application() noexcept { reset_or_terminate(); }

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&& other) noexcept
      : handle_(std::exchange(other.handle_, GNEISS_NULL_APPLICATION)) {}
  application& operator=(application&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_APPLICATION);
    }
    return *this;
  }

  [[nodiscard]] static result create(const gneiss_application_desc& desc,
                                     application& out_application) noexcept {
    gneiss_application handle = GNEISS_NULL_APPLICATION;
    const auto native_result = gneiss_application_create(&desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      application candidate;
      candidate.handle_ = handle;
      const auto closed = out_application.reset();
      if (closed.failed()) {
        return closed;
      }
      out_application.handle_ = candidate.release();
    }
    return from_native(native_result);
  }

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_APPLICATION; }
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }
  [[nodiscard]] result run(std::uint64_t max_frame_count = 0) noexcept {
    return from_native(gneiss_application_run(handle_, max_frame_count));
  }
  [[nodiscard]] result request_exit() noexcept {
    return from_native(gneiss_application_request_exit(handle_));
  }
  [[nodiscard]] result get_window_size(std::uint32_t& out_width,
                                       std::uint32_t& out_height) const noexcept {
    return from_native(gneiss_application_get_window_size(handle_, &out_width, &out_height));
  }
  [[nodiscard]] result set_paused(bool is_paused) noexcept {
    return from_native(gneiss_application_set_paused(handle_, is_paused ? UINT8_C(1) : UINT8_C(0)));
  }
  /** 取出当帧输入事件；队列为空返回 not_ready，仅限创建线程。 */
  [[nodiscard]] result poll_input(input_event& output) noexcept {
    return gneiss::poll_input(handle_, output);
  }
  /** 输出键盘值快照；仅限创建线程。 */
  [[nodiscard]] result get_keyboard_state(keyboard_state& output) const noexcept {
    return gneiss::get_keyboard_state(handle_, output);
  }
  /** 输出指针值快照；仅限创建线程。 */
  [[nodiscard]] result get_pointer_state(pointer_state& output) const noexcept {
    return gneiss::get_pointer_state(handle_, output);
  }
  /** 同步加载动作映射；成功使旧动作 ID 失效，失败保留原映射。 */
  [[nodiscard]] result load_action_map(std::string_view uri) noexcept {
    return gneiss::load_action_map(handle_, uri);
  }
  /** 返回非拥有动作 ID；失败保留 output，仅限创建线程。 */
  [[nodiscard]] result find_action(std::string_view name, action_id& output) const noexcept {
    return gneiss::find_action(handle_, name, output);
  }
  [[nodiscard]] result get_action_state(action_id id, action_state& output) const noexcept {
    return gneiss::get_action_state(handle_, id, output);
  }
  [[nodiscard]] result get_world(gneiss_world& out_world) const noexcept {
    return from_native(gneiss_application_get_world(handle_, &out_world));
  }
  /** 获取借用 World 视图，不转移所有权；场景切换或 Application 销毁可能使其失效。 */
  [[nodiscard]] result get_world(world_ref& output) const noexcept {
    gneiss_world value{};
    const auto status = from_native(gneiss_application_get_world(handle_, &value));
    if (status.ok()) {
      output = world_ref{value};
    }
    return status;
  }
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh& output) noexcept {
    return mesh::create(handle_, desc, output);
  }
  [[nodiscard]] result create_material(const material_desc& desc, material& output) noexcept {
    return material::create(handle_, desc, output);
  }
  [[nodiscard]] result create_texture(const texture_desc& desc, texture& output) noexcept {
    return texture::create(handle_, desc, output);
  }
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh_id& out_mesh) noexcept {
    gneiss_mesh handle = GNEISS_NULL_MESH;
    const auto native_result = gneiss_mesh_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_mesh = mesh_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_mesh(mesh_id mesh) noexcept {
    return from_native(gneiss_mesh_destroy(handle_, mesh.get()));
  }
  [[nodiscard]] result create_material(const material_desc& desc,
                                       material_id& out_material) noexcept {
    gneiss_material handle = GNEISS_NULL_MATERIAL;
    const auto native_result = gneiss_material_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_material = material_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_material(material_id material) noexcept {
    return from_native(gneiss_material_destroy(handle_, material.get()));
  }
  [[nodiscard]] result create_texture(const texture_desc& desc, texture_id& out_texture) noexcept {
    gneiss_texture handle = GNEISS_NULL_TEXTURE;
    const auto native_result = gneiss_texture_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_texture = texture_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_texture(texture_id texture) noexcept {
    return from_native(gneiss_texture_destroy(handle_, texture.get()));
  }
  [[nodiscard]] result submit_ui_draw_list(const ui_draw_list_desc& desc) noexcept {
    return from_native(gneiss_application_submit_ui_draw_list(handle_, &desc));
  }
  [[nodiscard]] result submit_debug_draw_list(const debug_draw_list_desc& desc) noexcept {
    return from_native(gneiss_application_submit_debug_draw_list(handle_, &desc));
  }
  [[nodiscard]] result log(const gneiss_log_message& message) noexcept {
    return from_native(gneiss_application_log(handle_, &message));
  }

  /** 幂等关闭；无效句柄视为已释放，其他失败保留句柄供所属线程重试。
   * 返回结果可供检查；保留直接 reset() 的既有调用方式。 */
  result reset() noexcept {
    if (handle_ == GNEISS_NULL_APPLICATION) {
      return result::success;
    }
    const auto status = from_native(gneiss_application_destroy(handle_));
    if (status.failed() && status != result::invalid_handle) {
      return status;
    }
    handle_ = GNEISS_NULL_APPLICATION;
    return result::success;
  }

  /** 转移原始句柄所有权，调用方负责在所属线程销毁。 */
  [[nodiscard]] gneiss_application release() noexcept {
    return std::exchange(handle_, GNEISS_NULL_APPLICATION);
  }

private:
  void reset_or_terminate() noexcept {
    if (reset().failed()) {
      std::terminate();
    }
  }
  gneiss_application handle_ = GNEISS_NULL_APPLICATION;
};

} // namespace gneiss

#endif
