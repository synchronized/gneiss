# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_ipc STATIC
  ipc_dispatcher.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/ipc/ipc_dispatcher.hpp"
  ipc_envelope.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/ipc/ipc_envelope.hpp"
  ipc_transport.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/ipc/ipc_transport.hpp"
)
target_link_libraries(
  gneiss_ipc
  PUBLIC gneiss_core_values
  PRIVATE gneiss_fatal gneiss_io_uv gneiss::libuv
)
target_include_directories(gneiss_ipc PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_features(gneiss_ipc PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_ipc)
gneiss_target_output_directories(gneiss_ipc)
