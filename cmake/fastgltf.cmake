# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include_guard(GLOBAL)

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/simdjson.cmake")

set(GNEISS_FASTGLTF_GIT_REPOSITORY
    "https://github.com/spnda/fastgltf.git"
    CACHE STRING "fastgltf Git 仓库")
set(GNEISS_FASTGLTF_GIT_TAG
    "0d1b67a28c4950ea2deb796702006dcbe31e02b3"
    CACHE STRING "fastgltf 锁定提交")

function(gneiss_resolve_fastgltf)
  if(TARGET fastgltf::fastgltf)
    return()
  endif()

  set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
  set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
  set(FASTGLTF_ENABLE_CPP_MODULES OFF CACHE BOOL "" FORCE)

  set(gneiss_fastgltf_exclude_from_all)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    list(APPEND gneiss_fastgltf_exclude_from_all EXCLUDE_FROM_ALL)
  endif()
  if(FETCHCONTENT_SOURCE_DIR_FASTGLTF)
    FetchContent_Declare(fastgltf ${gneiss_fastgltf_exclude_from_all})
  elseif(GNEISS_FASTGLTF_GIT_REPOSITORY STREQUAL "https://github.com/spnda/fastgltf.git"
      AND GNEISS_FASTGLTF_GIT_TAG STREQUAL "0d1b67a28c4950ea2deb796702006dcbe31e02b3")
    gneiss_cached_archive(fastgltf "https://codeload.github.com/spnda/fastgltf/tar.gz/0d1b67a28c4950ea2deb796702006dcbe31e02b3"
      "d497f7613d72e3dd921b42f6ce1b1ef545e8681084a74a70f5f2812610704c4e" CMakeLists.txt cached_source)
    FetchContent_Declare(fastgltf SOURCE_DIR "${cached_source}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/fastgltf-build" ${gneiss_fastgltf_exclude_from_all})
  else()
    if(GNEISS_DEPENDENCY_OFFLINE)
      message(FATAL_ERROR "自定义 fastgltf 离线构建需要 FETCHCONTENT_SOURCE_DIR_FASTGLTF")
    endif()
    FetchContent_Declare(fastgltf
      GIT_REPOSITORY "${GNEISS_FASTGLTF_GIT_REPOSITORY}" GIT_TAG "${GNEISS_FASTGLTF_GIT_TAG}"
      GIT_SHALLOW FALSE GIT_PROGRESS TRUE ${gneiss_fastgltf_exclude_from_all})
  endif()
  # 提前提供 target，禁止上游向共享源码 deps/simdjson 下载或写入。
  gneiss_resolve_simdjson()
  set(gneiss_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
  set(gneiss_saved_disable_find_simdjson "${CMAKE_DISABLE_FIND_PACKAGE_simdjson}")
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_DISABLE_FIND_PACKAGE_simdjson ON)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    FetchContent_MakeAvailable(fastgltf)
  else()
    FetchContent_GetProperties(fastgltf)
    if(NOT fastgltf_POPULATED)
      FetchContent_Populate(fastgltf)
      add_subdirectory("${fastgltf_SOURCE_DIR}" "${fastgltf_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
  endif()
  # 此版本上游仍生成安装导出；Gneiss 只在构建工具中使用私有 simdjson，不能泄漏该目标。
  if(TARGET gneiss_simdjson)
    get_target_property(fastgltf_links fastgltf LINK_LIBRARIES)
    list(TRANSFORM fastgltf_links REPLACE "^simdjson::simdjson$" "$<BUILD_INTERFACE:simdjson::simdjson>")
    set_property(TARGET fastgltf PROPERTY LINK_LIBRARIES "${fastgltf_links}")
    get_target_property(fastgltf_interface_links fastgltf INTERFACE_LINK_LIBRARIES)
    if(fastgltf_interface_links)
      set_property(TARGET fastgltf PROPERTY INTERFACE_LINK_LIBRARIES
        "$<BUILD_INTERFACE:${fastgltf_interface_links}>")
    endif()
  endif()
  set(BUILD_SHARED_LIBS "${gneiss_saved_build_shared_libs}")
  set(CMAKE_DISABLE_FIND_PACKAGE_simdjson "${gneiss_saved_disable_find_simdjson}")

  if(NOT TARGET fastgltf::fastgltf)
    message(FATAL_ERROR "fastgltf 未提供 fastgltf::fastgltf 目标")
  endif()
endfunction()
