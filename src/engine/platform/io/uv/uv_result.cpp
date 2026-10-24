// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_result.hpp"
#include "engine/core/result.hpp"

#include <uv.h>

namespace gneiss::engine::platform::io {

gneiss::engine::core::result from_uv_status(int value) noexcept {
  switch (value) {
  case 0:
    return gneiss::engine::core::result::success;
  case UV_EINVAL:
    return gneiss::engine::core::result::invalid_argument;
  case UV_ENOMEM:
    return gneiss::engine::core::result::out_of_memory;
  case UV_ENOSYS:
    return gneiss::engine::core::result::unsupported;
  case UV_EBUSY:
  case UV_EALREADY:
    return gneiss::engine::core::result::invalid_state;
  case UV_EAGAIN:
  case UV_ECANCELED:
    return gneiss::engine::core::result::not_ready;
  case UV_ENOENT:
    return gneiss::engine::core::result::not_found;
  default:
    return gneiss::engine::core::result::io;
  }
}

} // namespace gneiss::engine::platform::io
