# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/granit_version.cmake")

set(
  GNEISS_GRANIT_PROVIDER
  "AUTO"
  CACHE STRING
  "Granit 依赖来源：AUTO、PACKAGE 或 FETCH"
)
set_property(CACHE GNEISS_GRANIT_PROVIDER PROPERTY STRINGS AUTO PACKAGE FETCH)
set(
  GNEISS_GRANIT_GIT_REPOSITORY
  "https://github.com/synchronized/granit.git"
  CACHE STRING
  "FETCH 模式使用的 Granit Git 仓库"
)
function(gneiss_fetch_granit)
  if(GNEISS_BUILD_TOOLS OR GNEISS_BUILD_TESTING)
    set(GRANIT_BUILD_ASSET_TOOLS ON CACHE BOOL "" FORCE)
  endif()
  set(GRANIT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(GRANIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GRANIT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(GRANIT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(GRANIT_BUILD_INTEGRATION_SDL3 OFF CACHE BOOL "" FORCE)
  set(GRANIT_BUILD_INTEGRATION_IMGUI OFF CACHE BOOL "" FORCE)
  set(GRANIT_FETCH_INTEGRATION_DEPENDENCIES OFF CACHE BOOL "" FORCE)

  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    FetchContent_Declare(
      gneiss_granit
      # 缩短 MSBuild 中间文件路径，避免 AssetTools 长目标名触发 MAX_PATH。
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/granit"
      GIT_REPOSITORY "${GNEISS_GRANIT_GIT_REPOSITORY}"
      GIT_TAG "${GNEISS_GRANIT_GIT_TAG}"
      GIT_PROGRESS TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_GetProperties(gneiss_granit)
    if(NOT gneiss_granit_POPULATED)
      list(PREPEND CMAKE_MODULE_PATH "${gneiss_granit_SOURCE_DIR}/cmake")
      FetchContent_MakeAvailable(gneiss_granit)
    endif()
  else()
    FetchContent_Declare(
      gneiss_granit
      # 缩短 MSBuild 中间文件路径，避免 AssetTools 长目标名触发 MAX_PATH。
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/granit"
      GIT_REPOSITORY "${GNEISS_GRANIT_GIT_REPOSITORY}"
      GIT_TAG "${GNEISS_GRANIT_GIT_TAG}"
      GIT_PROGRESS TRUE
    )
    FetchContent_GetProperties(gneiss_granit)
    if(NOT gneiss_granit_POPULATED)
      FetchContent_Populate(gneiss_granit)
      list(PREPEND CMAKE_MODULE_PATH "${gneiss_granit_SOURCE_DIR}/cmake")
      add_subdirectory(
        "${gneiss_granit_SOURCE_DIR}" "${gneiss_granit_BINARY_DIR}" EXCLUDE_FROM_ALL
      )
    endif()
  endif()
endfunction()

function(gneiss_resolve_granit_runtime)
  set(granit_components Window RenderPipeline)
  if(GNEISS_BUILD_TOOLS OR GNEISS_BUILD_TESTING)
    list(APPEND granit_components AssetTools)
  endif()
  if(TARGET granit::granit AND TARGET granit::window AND
     TARGET granit::render_pipeline)
    if(NOT granit_RENDER_PIPELINE_ASSET_DIR)
      message(FATAL_ERROR "现有 Granit 0.30.0 targets 未提供 RenderPipeline 资产目录")
    endif()
    set(
      GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR
      "${granit_RENDER_PIPELINE_ASSET_DIR}"
      CACHE INTERNAL
      "Gneiss 使用的 Granit Render Pipeline 资产目录"
      FORCE
    )
    message(STATUS "Gneiss reuses the existing Granit runtime targets")
    return()
  endif()

  string(TOUPPER "${GNEISS_GRANIT_PROVIDER}" granit_provider)
  if(NOT granit_provider MATCHES "^(AUTO|PACKAGE|FETCH)$")
    message(
      FATAL_ERROR
        "GNEISS_GRANIT_PROVIDER must be AUTO, PACKAGE or FETCH; got '${GNEISS_GRANIT_PROVIDER}'"
    )
  endif()

  if(granit_provider STREQUAL "AUTO" OR granit_provider STREQUAL "PACKAGE")
    find_package(granit 0.30.0 CONFIG QUIET COMPONENTS ${granit_components})
    if(TARGET granit::granit AND TARGET granit::window AND
       TARGET granit::render_pipeline)
      set(
        GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR
        "${granit_RENDER_PIPELINE_ASSET_DIR}"
        CACHE INTERNAL
        "Gneiss 使用的 Granit Render Pipeline 资产目录"
        FORCE
      )
      message(STATUS "Gneiss uses the installed Granit runtime package")
      return()
    endif()
    if(granit_provider STREQUAL "PACKAGE")
      message(FATAL_ERROR "未找到 Granit 0.30.0 runtime package（含 Window、RenderPipeline）")
    endif()
    if(TARGET granit::granit)
      message(FATAL_ERROR "现有 Granit targets 缺少 Window 或 RenderPipeline，无法回退到 FETCH")
    endif()
  endif()

  message(
    STATUS
      "Gneiss fetches Granit from ${GNEISS_GRANIT_GIT_REPOSITORY} at ${GNEISS_GRANIT_GIT_TAG}"
  )
  gneiss_fetch_granit()
  FetchContent_GetProperties(gneiss_granit)
  set(
    GNEISS_GRANIT_FETCHED_BUILD_DIR
    "${gneiss_granit_BINARY_DIR}"
    CACHE INTERNAL
    "Gneiss FETCH 模式使用的 Granit 构建目录"
    FORCE
  )
  if(NOT granit_RENDER_PIPELINE_ASSET_DIR)
    message(FATAL_ERROR "下载的 Granit 0.30.0 未提供 RenderPipeline 资产目录")
  endif()
  set(
    GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR
    "${granit_RENDER_PIPELINE_ASSET_DIR}"
    CACHE INTERNAL
    "Gneiss 使用的 Granit Render Pipeline 资产目录"
    FORCE
  )
  if(NOT TARGET granit::granit OR NOT TARGET granit::window OR
     NOT TARGET granit::render_pipeline)
    message(FATAL_ERROR "下载的 Granit 未提供 runtime、Window 和 RenderPipeline 目标")
  endif()
endfunction()

function(gneiss_resolve_granit_tools)
  if(TARGET granit::asset_tools AND TARGET granit::granit)
    return()
  endif()
  if(TARGET granit::granit)
    message(FATAL_ERROR "Gneiss 工具或测试需要 Granit 0.30.0 AssetTools 组件")
  endif()
  string(TOUPPER "${GNEISS_GRANIT_PROVIDER}" granit_provider)
  if(NOT granit_provider MATCHES "^(AUTO|PACKAGE|FETCH)$")
    message(FATAL_ERROR "GNEISS_GRANIT_PROVIDER 必须为 AUTO、PACKAGE 或 FETCH")
  endif()
  if(NOT granit_provider STREQUAL "FETCH")
    find_package(granit 0.30.0 CONFIG QUIET COMPONENTS AssetTools)
    if(granit_FOUND AND TARGET granit::asset_tools AND TARGET granit::granit)
      return()
    endif()
    if(granit_provider STREQUAL "PACKAGE" OR TARGET granit::granit)
      message(FATAL_ERROR "未找到 Granit 0.30.0 AssetTools package")
    endif()
  endif()
  gneiss_fetch_granit()
endfunction()
