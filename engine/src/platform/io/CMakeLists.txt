# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_io_uv STATIC
  uv/uv_async_file.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_async_file.hpp"
  uv/uv_path_resolver.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_path_resolver.hpp"
  uv/uv_file_reader.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_file_reader.hpp"
  uv/uv_loop_access.hpp
  uv/uv_loop_executor.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/uv/uv_loop_executor.hpp"
  uv/uv_result.cpp
  uv/uv_result.hpp
)
target_link_libraries(
  gneiss_io_uv
  PUBLIC gneiss_core
  PRIVATE gneiss::libuv
)
target_include_directories(gneiss_io_uv PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/uv")
target_compile_features(gneiss_io_uv PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_io_uv)
gneiss_target_output_directories(gneiss_io_uv)
