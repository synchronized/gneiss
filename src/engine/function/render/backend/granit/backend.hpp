// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <memory>
#include <span>
namespace gneiss {
struct native_window_info;
struct render_device_stats;
struct render_frame_packet;
struct render_execution_result;
namespace render::backend::granit {
/** Granit 设备和 GPU 表示的唯一所有者；全部操作由 Service 的消费线程串行调用。
 * 不拥有执行器，不访问 World、资产管理器或 Service 的 RID 表。
 * 宿主销毁前须由 Service 在消费线程调用 shutdown。 */
class backend final {
public:
  backend();
  ~backend();
  backend(const backend&) = delete;
  backend& operator=(const backend&) = delete;
  error initialize(const native_window_info&, std::span<const std::byte>, float, float);
  error shutdown(render_device_stats&);
  error draw(render_frame_packet&, render_execution_result&);
  error set_environment(std::span<const std::byte>, float, float);
  /** initialize 完成后可读；不与设备初始化/关闭并发。 */
  texture_prepare_profile texture_profile() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
} // namespace render::backend::granit
} // namespace gneiss
