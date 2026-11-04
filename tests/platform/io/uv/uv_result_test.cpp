// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "uv_result.hpp"

#include <uv.h>

int main() {
  return gneiss::engine::from_uv_status(0) == gneiss::engine::error::success &&
                 gneiss::engine::from_uv_status(UV_EINVAL) ==
                     gneiss::engine::error::invalid_argument &&
                 gneiss::engine::from_uv_status(UV_ENOMEM) ==
                     gneiss::engine::error::out_of_memory &&
                 gneiss::engine::from_uv_status(UV_ENOSYS) == gneiss::engine::error::unsupported &&
                 gneiss::engine::from_uv_status(UV_EBUSY) == gneiss::engine::error::invalid_state &&
                 gneiss::engine::from_uv_status(UV_EAGAIN) == gneiss::engine::error::not_ready &&
                 gneiss::engine::from_uv_status(UV_ENOENT) == gneiss::engine::error::not_found &&
                 gneiss::engine::from_uv_status(UV_ECONNREFUSED) == gneiss::engine::error::io
             ? 0
             : 1;
}
