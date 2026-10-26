// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/render_service.hpp"
namespace gneiss::engine {
struct render_service::implementation {
  render_resource_service resources;
};
render_service::render_service() : impl_(std::make_unique<implementation>()) {}
render_service::~render_service() = default;
render_resource_service& render_service::resources() noexcept { return impl_->resources; }
result render_service::initialize(const native_window_info&, std::span<const std::byte>, float,
                                  float, log_dispatcher*) noexcept {
  return result::unsupported;
}
result render_service::request_environment(std::shared_ptr<const std::vector<std::byte>>, float,
                                           float) noexcept {
  return result::unsupported;
}
result render_service::shutdown(render_device_stats& stats) noexcept {
  impl_->resources.close();
  stats = {};
  return result::success;
}
result render_service::finish_frames() noexcept { return result::success; }
void render_service::set_completion_notification(std::shared_ptr<progress_notification>) noexcept {}
result render_service::prepare_frame_packet_storage(render_frame_packet&, bool& prepare) noexcept {
  prepare = false;
  return result::unsupported;
}
result render_service::submit(render_frame_packet, render_frame_policy,
                              std::uint64_t* sequence) noexcept {
  if (sequence)
    *sequence = 0U;
  return result::unsupported;
}
bool render_service::try_take_required_completion(render_frame_completion&) noexcept {
  return false;
}
render_queue_stats render_service::query_performance_stats() const noexcept { return {}; }
texture_prepare_profile render_service::texture_profile() const noexcept { return {}; }
}
