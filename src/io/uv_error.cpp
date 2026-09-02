// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_error.h"

#include <uv.h>

namespace gneiss {

result from_uv_error(int value) noexcept {
  switch (value) {
  case 0:
    return result::success;
  case UV_EINVAL:
    return result::invalid_argument;
  case UV_ENOMEM:
    return result::out_of_memory;
  case UV_ENOSYS:
    return result::unsupported;
  case UV_EBUSY:
  case UV_EALREADY:
    return result::invalid_state;
  case UV_EAGAIN:
  case UV_ECANCELED:
    return result::not_ready;
  case UV_ENOENT:
    return result::not_found;
  default:
    return result::io;
  }
}

} // namespace gneiss
