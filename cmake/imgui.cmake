# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/dependency_source_cache.cmake")

function(gneiss_resolve_imgui)
  if(TARGET gneiss_imgui)
    return()
  endif()

  if(NOT FETCHCONTENT_SOURCE_DIR_GNEISS_IMGUI_SOURCE)
    gneiss_cached_archive(imgui
      "https://codeload.github.com/ocornut/imgui/tar.gz/fd13a1e8923a0a7077b404fc36fd063b25a0c0b5"
      "5a7cca7ceff10b7d4d800d7e679d8a5295135567c1e9058f00d44d4e55da44e8" imgui.cpp cached_source)
    FetchContent_Declare(gneiss_imgui_source SOURCE_DIR "${cached_source}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_imgui_source-build"
      SOURCE_SUBDIR gneiss-no-cmake)
  else()
    FetchContent_Declare(gneiss_imgui_source
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/gneiss_imgui_source-build"
      SOURCE_SUBDIR gneiss-no-cmake)
  endif()
  FetchContent_MakeAvailable(gneiss_imgui_source)

  add_library(
    gneiss_imgui STATIC
    "${gneiss_imgui_source_SOURCE_DIR}/imgui.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_demo.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_draw.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_tables.cpp"
    "${gneiss_imgui_source_SOURCE_DIR}/imgui_widgets.cpp"
  )
  target_include_directories(gneiss_imgui SYSTEM PUBLIC "${gneiss_imgui_source_SOURCE_DIR}")
  target_compile_features(gneiss_imgui PUBLIC cxx_std_11)
  gneiss_target_output_directories(gneiss_imgui)
endfunction()
