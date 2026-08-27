# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include_guard(GLOBAL)

include(FetchContent)

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
  FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY "${GNEISS_FASTGLTF_GIT_REPOSITORY}"
    GIT_TAG "${GNEISS_FASTGLTF_GIT_TAG}"
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
    ${gneiss_fastgltf_exclude_from_all}
  )
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
  set(BUILD_SHARED_LIBS "${gneiss_saved_build_shared_libs}")
  set(CMAKE_DISABLE_FIND_PACKAGE_simdjson "${gneiss_saved_disable_find_simdjson}")

  if(NOT TARGET fastgltf::fastgltf)
    message(FATAL_ERROR "fastgltf 未提供 fastgltf::fastgltf 目标")
  endif()
endfunction()
