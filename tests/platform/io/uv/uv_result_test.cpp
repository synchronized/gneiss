// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "uv_result.hpp"

#include <uv.h>

int main(int argc, char**) {
  if (argc > 1) {
    (void)gneiss::from_uv_status(UV_ENOMEM);
    return 0;
  }
  return gneiss::from_uv_status(0) == gneiss::error::success &&
                 gneiss::from_uv_status(UV_EINVAL) ==
                     gneiss::error::invalid_argument &&
                 gneiss::from_uv_status(UV_ENOSYS) == gneiss::error::unsupported &&
                 gneiss::from_uv_status(UV_EBUSY) == gneiss::error::invalid_state &&
                 gneiss::from_uv_status(UV_EAGAIN) == gneiss::error::not_ready &&
                 gneiss::from_uv_status(UV_ENOENT) == gneiss::error::not_found &&
                 gneiss::from_uv_status(UV_ECONNREFUSED) == gneiss::error::io
             ? 0
             : 1;
}
