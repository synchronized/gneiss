// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_result.hpp"
#include "engine/core/error.hpp"

#include <uv.h>

namespace gneiss::engine {

error from_uv_status(int value) noexcept {
  switch (value) {
  case 0:
    return error::success;
  case UV_EINVAL:
    return error::invalid_argument;
  case UV_ENOMEM:
    return error::out_of_memory;
  case UV_ENOSYS:
    return error::unsupported;
  case UV_EEXIST:
  case UV_EBUSY:
  case UV_EALREADY:
    return error::invalid_state;
  case UV_EAGAIN:
  case UV_ECANCELED:
    return error::not_ready;
  case UV_ENOENT:
    return error::not_found;
  default:
    return error::io;
  }
}

} // namespace gneiss::engine
