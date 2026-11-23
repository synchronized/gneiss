# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_files service.cpp read_stream.cpp backends/memory.cpp)
add_library(gneiss::files ALIAS gneiss_files)
target_link_libraries(gneiss_files PUBLIC gneiss_base PRIVATE gneiss_libassert)
target_include_directories(gneiss_files PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_files PRIVATE GNEISS_BUILDING_FILES
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_FILES_STATIC_DEFINE>)
set_target_properties(gneiss_files PROPERTIES EXPORT_NAME files POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  target_sources(gneiss_files PRIVATE backends/native.cpp)
  target_link_libraries(gneiss_files PUBLIC gneiss_io
    PRIVATE "$<BUILD_INTERFACE:gneiss::libuv>" "$<INSTALL_INTERFACE:gneiss::uv>")
endif()
if(EMSCRIPTEN)
  target_sources(gneiss_files PRIVATE backends/detail/fetch_stream.cpp backends/fetch.cpp backends/browser.cpp)
  target_compile_options(gneiss_files PUBLIC -fexceptions)
  target_link_options(gneiss_files PUBLIC -fexceptions "-sFETCH=1" "-lidbfs.js"
    "$<BUILD_INTERFACE:SHELL:--js-library \"${CMAKE_CURRENT_SOURCE_DIR}/backends/detail/fetch_stream.js\">")
endif()
gneiss_target_compile_warnings(gneiss_files)
gneiss_target_output_directories(gneiss_files)
