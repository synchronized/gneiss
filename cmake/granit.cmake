# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)

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
set(
  GNEISS_GRANIT_GIT_TAG
  "d5aa1cceef0741c17ff58eac5f14f731a3991bcb"
  CACHE STRING
  "FETCH 模式锁定的 Granit Git tag 或完整提交"
)

function(gneiss_fetch_granit)
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
  if(TARGET granit::granit AND TARGET granit::window AND TARGET granit::input AND
     TARGET granit::render_pipeline)
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
    find_package(granit 0.4 CONFIG QUIET COMPONENTS Window Input RenderPipeline)
    if(TARGET granit::granit AND TARGET granit::window AND TARGET granit::input AND
       TARGET granit::render_pipeline)
      message(STATUS "Gneiss uses the installed Granit runtime package")
      return()
    endif()
    if(granit_provider STREQUAL "PACKAGE")
      message(FATAL_ERROR "未找到 Granit 0.4 runtime package（含 Window、Input、RenderPipeline）")
    endif()
    if(TARGET granit::granit)
      message(FATAL_ERROR "现有 Granit targets 缺少 Window、Input 或 RenderPipeline，无法回退到 FETCH")
    endif()
  endif()

  message(
    STATUS
      "Gneiss fetches Granit from ${GNEISS_GRANIT_GIT_REPOSITORY} at ${GNEISS_GRANIT_GIT_TAG}"
  )
  gneiss_fetch_granit()
  if(NOT TARGET granit::granit OR NOT TARGET granit::window OR NOT TARGET granit::input OR
     NOT TARGET granit::render_pipeline)
    message(FATAL_ERROR "下载的 Granit 未提供 runtime、Window、Input 和 RenderPipeline 目标")
  endif()
endfunction()
