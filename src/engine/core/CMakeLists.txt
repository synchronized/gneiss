# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 仅供内部目标复用已有值类型，不链接 Engine、不安装，也不提供独立运行时。
# result::message() 属于 Engine 的 C ABI，底层模块只使用结果值与比较操作。
add_library(gneiss_core_values INTERFACE)
target_include_directories(gneiss_core_values INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")
target_compile_features(gneiss_core_values INTERFACE cxx_std_20)
target_compile_definitions(gneiss_core_values INTERFACE
  $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>)

add_subdirectory(tasks)
