# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

function(gneiss_resolve_libuv)
  if(TARGET gneiss_libuv)
    return()
  endif()

  set(LIBUV_BUILD_SHARED "${BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
  set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)

  # 显式源码覆盖优先，缓存路径不写入覆盖变量，避免版本升级被旧 CMakeCache 固定。
  if(NOT FETCHCONTENT_SOURCE_DIR_GNEISS_LIBUV_SOURCE)
    gneiss_cached_archive(libuv
      "https://dist.libuv.org/dist/v1.52.1/libuv-v1.52.1.tar.gz"
      "66d511b9e6e334c0e62279eb234fbfb2b3110b1479c09b95b44c7afca8cff9e7"
      CMakeLists.txt libuv_source)
    FetchContent_Declare(gneiss_libuv_source SOURCE_DIR "${libuv_source}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_libuv_source-build")
  else()
    FetchContent_Declare(gneiss_libuv_source
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_libuv_source-build")
  endif()

  # 所有 IO 消费者使用同一 libuv；共享构建不能各自静态嵌入运行状态。
  set(gneiss_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_MakeAvailable(gneiss_libuv_source)
  set(BUILD_SHARED_LIBS "${gneiss_saved_build_shared_libs}")
  # 仅构建实际使用的目标；安装由 Gneiss 的部署闭包统一管理。
  set_property(DIRECTORY "${gneiss_libuv_source_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)

  add_library(gneiss_libuv INTERFACE)
  add_library(gneiss::libuv ALIAS gneiss_libuv)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(gneiss_libuv INTERFACE uv)
    gneiss_target_output_directories(uv)
    set_target_properties(uv PROPERTIES FOLDER "ThirdParty/Libuv")
  else()
    target_link_libraries(gneiss_libuv INTERFACE uv_a)
  endif()
  set(GNEISS_LIBUV_SOURCE_DIR "${gneiss_libuv_source_SOURCE_DIR}" PARENT_SCOPE)
  if(TARGET uv_a)
    set_target_properties(uv_a PROPERTIES FOLDER "ThirdParty/Libuv")
  endif()

  get_target_property(LIBUV_UV_SOURCES uv_a SOURCES)
  foreach(SOURCE_FILE ${LIBUV_UV_SOURCES})
    # Create a clean target name
    string(REPLACE "/" "-" TARGET_NAME ${SOURCE_FILE})
    string(REGEX REPLACE "\\.[^.]*$" "" TARGET_NAME ${TARGET_NAME})
    set(FULL_TARGET_NAME "tidy-${TARGET_NAME}")

    # Create individual target for this source file
    if(TARGET ${FULL_TARGET_NAME})
      set_target_properties(${FULL_TARGET_NAME} PROPERTIES FOLDER "ThirdParty/Libuv/Tidy")
    endif()
  endforeach()
  set(libuv_target_list tidy tidy-src)
  foreach(tidy_target IN LISTS libuv_target_list)
    if(TARGET ${tidy_target})
      set_target_properties(${tidy_target} PROPERTIES FOLDER "ThirdParty/Libuv/Tidy")
    endif()
  endforeach()

endfunction()

