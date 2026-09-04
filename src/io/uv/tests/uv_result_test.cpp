// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_result.h"

#include <uv.h>

int main() {
  return gneiss::io_internal::from_uv_status(0) == gneiss::result::success &&
                 gneiss::io_internal::from_uv_status(UV_EINVAL) ==
                     gneiss::result::invalid_argument &&
                 gneiss::io_internal::from_uv_status(UV_ENOMEM) == gneiss::result::out_of_memory &&
                 gneiss::io_internal::from_uv_status(UV_ENOSYS) == gneiss::result::unsupported &&
                 gneiss::io_internal::from_uv_status(UV_EBUSY) == gneiss::result::invalid_state &&
                 gneiss::io_internal::from_uv_status(UV_EAGAIN) == gneiss::result::not_ready &&
                 gneiss::io_internal::from_uv_status(UV_ENOENT) == gneiss::result::not_found &&
                 gneiss::io_internal::from_uv_status(UV_ECONNREFUSED) == gneiss::result::io
             ? 0
             : 1;
}
