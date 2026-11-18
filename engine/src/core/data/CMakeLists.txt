# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_data value.cpp value_edit.cpp)
add_library(gneiss::data ALIAS gneiss_data)
target_link_libraries(gneiss_data PUBLIC gneiss_math)
target_compile_definitions(gneiss_data PRIVATE GNEISS_BUILDING_DATA
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_DATA_STATIC_DEFINE>)
set_target_properties(gneiss_data PROPERTIES EXPORT_NAME data POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_data)
gneiss_target_output_directories(gneiss_data)
