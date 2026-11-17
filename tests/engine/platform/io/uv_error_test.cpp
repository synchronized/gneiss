// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "platform/io/detail/uv_error.hpp"

#include <uv.h>

int main(int argc, char**) {
  if (argc > 1) {
    (void)gneiss::io::detail::from_uv_status(UV_ENOMEM);
    return 0;
  }
  return gneiss::io::detail::from_uv_status(0) == gneiss::error::success &&
                 gneiss::io::detail::from_uv_status(UV_EINVAL) ==
                     gneiss::error::invalid_argument &&
                 gneiss::io::detail::from_uv_status(UV_ENOSYS) == gneiss::error::unsupported &&
                 gneiss::io::detail::from_uv_status(UV_EBUSY) == gneiss::error::invalid_state &&
                 gneiss::io::detail::from_uv_status(UV_EAGAIN) == gneiss::error::not_ready &&
                 gneiss::io::detail::from_uv_status(UV_ENOENT) == gneiss::error::not_found &&
                 gneiss::io::detail::from_uv_status(UV_ECONNREFUSED) == gneiss::error::io
             ? 0
             : 1;
}
