// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "uv_async_file.hpp"
#include <caors/io/async_file.h>
namespace gneiss::engine::platform::caors {
inline ::caors::io::io_error translate(result status) {
  using ::caors::io::io_errc;

  if (status == result::success)
    return {};
  if (status == result::not_found)
    return {io_errc::not_found};
  if (status == result::invalid_state)
    return {io_errc::closed};
  if (status == result::invalid_argument)
    return {io_errc::invalid_argument};
  if (status == result::out_of_memory)
    return {io_errc::out_of_memory};
  if (status == result::not_ready)
    return {io_errc::pending};

  return {io_errc::backend_failure, 1, static_cast<gneiss_result>(status)};
}

}
