// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_result.hpp"
#include "engine/core/result.hpp"

#include <uv.h>

namespace gneiss::engine {

gneiss::engine::result from_uv_status(int value) noexcept {
  switch (value) {
  case 0:
    return gneiss::engine::result::success;
  case UV_EINVAL:
    return gneiss::engine::result::invalid_argument;
  case UV_ENOMEM:
    return gneiss::engine::result::out_of_memory;
  case UV_ENOSYS:
    return gneiss::engine::result::unsupported;
  case UV_EBUSY:
  case UV_EALREADY:
    return gneiss::engine::result::invalid_state;
  case UV_EAGAIN:
  case UV_ECANCELED:
    return gneiss::engine::result::not_ready;
  case UV_ENOENT:
    return gneiss::engine::result::not_found;
  default:
    return gneiss::engine::result::io;
  }
}

} // namespace gneiss::engine
