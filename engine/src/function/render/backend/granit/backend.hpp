// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/resource/texture/texture_data.hpp>
#include <memory>
#include <span>
namespace gneiss {
namespace window {
struct native_handle;
struct surface_state;
}
namespace render {
struct device_stats;
struct execution_result;
}
namespace render::detail {
struct frame_packet;
}

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
  error initialize(const gneiss::window::native_handle&, window::surface_state,
                   std::span<const std::byte>, float, float);
  error shutdown(gneiss::render::device_stats&);
  void destroy_target(rid target);
  error draw(gneiss::render::detail::frame_packet&, gneiss::render::execution_result&);
  error set_environment(std::span<const std::byte>, float, float);
  /** initialize 完成后可读；不与设备初始化/关闭并发。 */
  texture_prepare_profile texture_profile() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
} // namespace render::backend::granit
} // namespace gneiss
