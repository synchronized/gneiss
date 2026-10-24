// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "uv_result.hpp"

#include <uv.h>

int main() {
  return gneiss::engine::from_uv_status(0) == gneiss::engine::result::success &&
                 gneiss::engine::from_uv_status(UV_EINVAL) ==
                     gneiss::engine::result::invalid_argument &&
                 gneiss::engine::from_uv_status(UV_ENOMEM) ==
                     gneiss::engine::result::out_of_memory &&
                 gneiss::engine::from_uv_status(UV_ENOSYS) == gneiss::engine::result::unsupported &&
                 gneiss::engine::from_uv_status(UV_EBUSY) ==
                     gneiss::engine::result::invalid_state &&
                 gneiss::engine::from_uv_status(UV_EAGAIN) == gneiss::engine::result::not_ready &&
                 gneiss::engine::from_uv_status(UV_ENOENT) == gneiss::engine::result::not_found &&
                 gneiss::engine::from_uv_status(UV_ECONNREFUSED) == gneiss::engine::result::io
             ? 0
             : 1;
}
