# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_io STATIC
  executor.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/executor.hpp"
  detail/uv_loop_access.hpp
  detail/uv_error.hpp
  detail/uv_error.cpp
)
target_link_libraries(gneiss_io PUBLIC gneiss_core PRIVATE gneiss::libuv)
target_compile_features(gneiss_io PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_io)
gneiss_target_output_directories(gneiss_io)
