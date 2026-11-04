# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

function(gneiss_resolve_libuv)
  if(TARGET gneiss_libuv)
    return()
  endif()

  set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
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

# 核心静态安装包内嵌 libuv 对象，避免向消费者泄漏未安装的第三方目标。
function(gneiss_enable_asset_libuv target)
  if(NOT GNEISS_TASK_THREADS OR EMSCRIPTEN)
    return()
  endif()
  if(NOT TARGET gneiss_libuv_objects)
    get_target_property(uv_source_dir uv_a SOURCE_DIR)
    get_target_property(uv_source_files uv_a SOURCES)
    set(uv_absolute_sources)
    foreach(uv_source IN LISTS uv_source_files)
      if(IS_ABSOLUTE "${uv_source}")
        list(APPEND uv_absolute_sources "${uv_source}")
      else()
        list(APPEND uv_absolute_sources "${uv_source_dir}/${uv_source}")
      endif()
    endforeach()
    add_library(gneiss_libuv_objects OBJECT EXCLUDE_FROM_ALL ${uv_absolute_sources})
    target_include_directories(gneiss_libuv_objects PRIVATE
      $<TARGET_PROPERTY:uv_a,INCLUDE_DIRECTORIES>)
    target_compile_definitions(gneiss_libuv_objects PRIVATE
      $<TARGET_PROPERTY:uv_a,COMPILE_DEFINITIONS>)
    target_compile_options(gneiss_libuv_objects PRIVATE
      $<TARGET_PROPERTY:uv_a,COMPILE_OPTIONS>)
    set_target_properties(gneiss_libuv_objects PROPERTIES POSITION_INDEPENDENT_CODE YES C_VISIBILITY_PRESET hidden)
  endif()
  get_target_property(uv_system_libraries uv_a LINK_LIBRARIES)
  target_sources(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/src/engine/platform/files/native_file_backend.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/platform/io/uv/uv_file_reader.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/platform/io/uv/uv_async_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/platform/io/uv/uv_path_resolver.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/platform/io/uv/uv_loop_executor.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/platform/io/uv/uv_result.cpp"
    $<TARGET_OBJECTS:gneiss_libuv_objects>)
  target_include_directories(${target} PRIVATE $<TARGET_PROPERTY:uv_a,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(${target} PRIVATE GNEISS_ASSET_LIBUV)
  # 仅系统链接库保留在静态消费者的 link-only 闭包中。
  if(uv_system_libraries)
    target_link_libraries(${target} PRIVATE ${uv_system_libraries})
  endif()
endfunction()
