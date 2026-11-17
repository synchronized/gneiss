# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_ipc STATIC
  envelope.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/ipc/envelope.hpp"
  transport.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/ipc/transport.hpp"
)
target_link_libraries(
  gneiss_ipc
  PUBLIC gneiss_core
  PRIVATE gneiss_io gneiss::libuv
)
target_include_directories(gneiss_ipc PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_features(gneiss_ipc PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_ipc)
gneiss_target_output_directories(gneiss_ipc)
