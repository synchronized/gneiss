# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)

function(gneiss_resolve_libuv)
  if(TARGET gneiss_libuv)
    return()
  endif()

  set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    gneiss_libuv_source
    # libuv 1.52.1；MIT 许可。官方发布归档与摘要共同确保依赖解析可复现。
    URL https://dist.libuv.org/dist/v1.52.1/libuv-v1.52.1.tar.gz
    URL_HASH SHA256=66d511b9e6e334c0e62279eb234fbfb2b3110b1479c09b95b44c7afca8cff9e7
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )

  # libuv 的别名目标跟随 BUILD_SHARED_LIBS；Gneiss 始终私有链接静态目标。
  set(gneiss_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_MakeAvailable(gneiss_libuv_source)
  set(BUILD_SHARED_LIBS "${gneiss_saved_build_shared_libs}")
  # 私有静态依赖不参与默认构建，也不把上游头、库或 package 安装给 Gneiss Consumer。
  set_property(DIRECTORY "${gneiss_libuv_source_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)

  add_library(gneiss_libuv INTERFACE)
  add_library(gneiss::libuv ALIAS gneiss_libuv)
  target_link_libraries(gneiss_libuv INTERFACE uv_a)
  set(GNEISS_LIBUV_SOURCE_DIR "${gneiss_libuv_source_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
