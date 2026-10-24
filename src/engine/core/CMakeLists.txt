# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 仅供内部目标复用已有值类型，不链接 Engine、不安装，也不提供独立运行时。
# 内部结果值通过受限清单共享；静态文本实现由 Engine 提供。
include(internal_headers)
gneiss_internal_headers(gneiss_result_interface "${PROJECT_SOURCE_DIR}/src" engine/core/result.hpp engine/core/export.hpp)
add_library(gneiss_core_values INTERFACE)
target_link_libraries(gneiss_core_values INTERFACE gneiss_result_interface gneiss_value_interface)
target_compile_features(gneiss_core_values INTERFACE cxx_std_20)
target_compile_definitions(gneiss_core_values INTERFACE
  $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>)

add_subdirectory(tasks)
