// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_result.hpp"

#include <uv.h>

int main() {
  return gneiss::engine::platform::io::from_uv_status(0) == gneiss::result::success &&
                 gneiss::engine::platform::io::from_uv_status(UV_EINVAL) ==
                     gneiss::result::invalid_argument &&
                 gneiss::engine::platform::io::from_uv_status(UV_ENOMEM) ==
                     gneiss::result::out_of_memory &&
                 gneiss::engine::platform::io::from_uv_status(UV_ENOSYS) ==
                     gneiss::result::unsupported &&
                 gneiss::engine::platform::io::from_uv_status(UV_EBUSY) ==
                     gneiss::result::invalid_state &&
                 gneiss::engine::platform::io::from_uv_status(UV_EAGAIN) ==
                     gneiss::result::not_ready &&
                 gneiss::engine::platform::io::from_uv_status(UV_ENOENT) ==
                     gneiss::result::not_found &&
                 gneiss::engine::platform::io::from_uv_status(UV_ECONNREFUSED) == gneiss::result::io
             ? 0
             : 1;
}
