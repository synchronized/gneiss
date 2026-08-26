// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_
#define GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_

#include "platform/granit/granit_platform.h"

#include <gneiss/core/result.h>

#include <granit/granit.hpp>

namespace gneiss::application_internal {

class granit_render_service final {
public:
  [[nodiscard]] gneiss_result initialize(const native_window_info& window) noexcept;
  [[nodiscard]] gneiss_result render(native_window_info& window) noexcept;

private:
  granit::renderer renderer_;
  granit::surface surface_;
  granit::swapchain swapchain_;
  granit::frame_context frame_context_;
};

} // namespace gneiss::application_internal

#endif
