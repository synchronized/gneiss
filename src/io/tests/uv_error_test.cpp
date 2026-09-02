// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_error.h"

#include <uv.h>

int main() {
  return gneiss::from_uv_error(0) == gneiss::result::success &&
                 gneiss::from_uv_error(UV_EINVAL) == gneiss::result::invalid_argument &&
                 gneiss::from_uv_error(UV_ENOMEM) == gneiss::result::out_of_memory &&
                 gneiss::from_uv_error(UV_ENOSYS) == gneiss::result::unsupported &&
                 gneiss::from_uv_error(UV_EBUSY) == gneiss::result::invalid_state &&
                 gneiss::from_uv_error(UV_EAGAIN) == gneiss::result::not_ready &&
                 gneiss::from_uv_error(UV_ENOENT) == gneiss::result::not_found &&
                 gneiss::from_uv_error(UV_ECONNREFUSED) == gneiss::result::io
             ? 0
             : 1;
}
