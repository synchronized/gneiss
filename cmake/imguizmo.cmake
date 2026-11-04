# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

function(gneiss_resolve_imguizmo)
  if(TARGET gneiss_imguizmo)
    return()
  endif()

  if(NOT FETCHCONTENT_SOURCE_DIR_GNEISS_IMGUIZMO_SOURCE)
    gneiss_cached_archive(imguizmo
      "https://codeload.github.com/CedricGuillemet/ImGuizmo/tar.gz/18cef5e031d8c6973d80284c67f60549fafd78c1"
      "6ad626f0687be12c2f3ba6542c0f1bdda9e71e395d0645e4cda37695354406d8" README.md cached_source)
    FetchContent_Declare(gneiss_imguizmo_source SOURCE_DIR "${cached_source}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_imguizmo_source-build"
      SOURCE_SUBDIR gneiss-no-cmake)
  else()
    FetchContent_Declare(gneiss_imguizmo_source
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_imguizmo_source-build"
      SOURCE_SUBDIR gneiss-no-cmake)
  endif()
  FetchContent_MakeAvailable(gneiss_imguizmo_source)

  add_library(gneiss_imguizmo STATIC "${gneiss_imguizmo_source_SOURCE_DIR}/src/ImGuizmo.cpp")
  target_include_directories(gneiss_imguizmo SYSTEM PUBLIC
                             "${gneiss_imguizmo_source_SOURCE_DIR}/src")
  target_link_libraries(gneiss_imguizmo PUBLIC gneiss_imgui)
  target_compile_features(gneiss_imguizmo PUBLIC cxx_std_11)
  gneiss_target_output_directories(gneiss_imguizmo)
endfunction()
