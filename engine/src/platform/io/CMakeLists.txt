# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_io
  executor.cpp
  "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/io/executor.hpp"
  detail/uv_loop_access.hpp
  detail/uv_error.hpp
  detail/uv_error.cpp
)
target_link_libraries(gneiss_io PUBLIC gneiss_base PRIVATE "$<BUILD_INTERFACE:gneiss::libuv>" "$<INSTALL_INTERFACE:gneiss::uv>")
target_compile_features(gneiss_io PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_io)
gneiss_target_output_directories(gneiss_io)

add_library(gneiss::io ALIAS gneiss_io)
target_compile_definitions(gneiss_io PRIVATE GNEISS_BUILDING_IO
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_IO_STATIC_DEFINE>)
set_target_properties(gneiss_io PROPERTIES EXPORT_NAME io POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
