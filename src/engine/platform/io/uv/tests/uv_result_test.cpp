// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "uv_result.hpp"

#include <uv.h>

int main() {
  return gneiss::engine::platform::io::from_uv_status(0) == gneiss::engine::core::result::success &&
                 gneiss::engine::platform::io::from_uv_status(UV_EINVAL) ==
                     gneiss::engine::core::result::invalid_argument &&
                 gneiss::engine::platform::io::from_uv_status(UV_ENOMEM) ==
                     gneiss::engine::core::result::out_of_memory &&
                 gneiss::engine::platform::io::from_uv_status(UV_ENOSYS) ==
                     gneiss::engine::core::result::unsupported &&
                 gneiss::engine::platform::io::from_uv_status(UV_EBUSY) ==
                     gneiss::engine::core::result::invalid_state &&
                 gneiss::engine::platform::io::from_uv_status(UV_EAGAIN) ==
                     gneiss::engine::core::result::not_ready &&
                 gneiss::engine::platform::io::from_uv_status(UV_ENOENT) ==
                     gneiss::engine::core::result::not_found &&
                 gneiss::engine::platform::io::from_uv_status(UV_ECONNREFUSED) ==
                     gneiss::engine::core::result::io
             ? 0
             : 1;
}
