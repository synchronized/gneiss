# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_window service.cpp)
add_library(gneiss::window ALIAS gneiss_window)
target_link_libraries(gneiss_window PUBLIC gneiss_base PRIVATE granit::window gneiss_libassert)
target_compile_definitions(gneiss_window PRIVATE GNEISS_BUILDING_WINDOW
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_WINDOW_STATIC_DEFINE>)
set_target_properties(gneiss_window PROPERTIES EXPORT_NAME window POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_window)
gneiss_target_output_directories(gneiss_window)
