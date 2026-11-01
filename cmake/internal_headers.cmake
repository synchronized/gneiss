# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include_guard(GLOBAL)

# 显式导出同源码模块契约，不安装、不生成另一套实现。复制而非转发 include，
# 避免源目录相对包含意外穿透隔离。configure_file 跟踪原头变更并触发重配。
function(gneiss_internal_headers target source_root)
  add_library(${target} INTERFACE)
  target_compile_features(${target} INTERFACE cxx_std_20)
  # 清单变化使用新目录，移除的头不会因旧构建缓存继续可见。
  string(SHA256 manifest_hash "${ARGN}")
  set(export_root "${CMAKE_BINARY_DIR}/internal-include/${target}/${manifest_hash}")
  foreach(header IN LISTS ARGN)
    if(IS_ABSOLUTE "${header}" OR header MATCHES "[.][.]")
      message(FATAL_ERROR "内部接口必须使用模块根下的相对路径：${header}")
    endif()
    configure_file("${source_root}/${header}" "${export_root}/${header}" COPYONLY)
  endforeach()
  target_include_directories(${target} INTERFACE "$<BUILD_INTERFACE:${export_root}>")
endfunction()
