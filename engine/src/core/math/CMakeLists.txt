# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_math transform.cpp camera.cpp)
add_library(gneiss::math ALIAS gneiss_math)
target_include_directories(gneiss_math PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>")
target_compile_features(gneiss_math PUBLIC cxx_std_20)
target_link_libraries(gneiss_math PUBLIC "$<BUILD_INTERFACE:gneiss_math_dependency>")
target_compile_definitions(gneiss_math
  PRIVATE GNEISS_BUILDING_MATH
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_MATH_STATIC_DEFINE>)
set_target_properties(gneiss_math PROPERTIES
  EXPORT_NAME math
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_math)
gneiss_target_output_directories(gneiss_math)
