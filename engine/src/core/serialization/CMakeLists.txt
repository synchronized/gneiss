# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_serialization context.cpp serialization.cpp traversal.cpp value.cpp
  builtin_registration.cpp math_registration.cpp)
add_library(gneiss::serialization ALIAS gneiss_serialization)
target_link_libraries(gneiss_serialization PUBLIC gneiss_meta PRIVATE gneiss_libassert)
target_include_directories(gneiss_serialization PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_serialization PRIVATE GNEISS_BUILDING_SERIALIZATION
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_SERIALIZATION_STATIC_DEFINE>)
set_target_properties(gneiss_serialization PROPERTIES EXPORT_NAME serialization
  POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_serialization)
gneiss_target_output_directories(gneiss_serialization)

add_library(gneiss_serialization_json json.cpp)
add_library(gneiss::serialization_json ALIAS gneiss_serialization_json)
target_link_libraries(gneiss_serialization_json PUBLIC gneiss_serialization
  PRIVATE "$<BUILD_INTERFACE:gneiss_yyjson>" gneiss_libassert)
target_include_directories(gneiss_serialization_json PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_serialization_json PRIVATE GNEISS_BUILDING_JSON
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_JSON_STATIC_DEFINE>)
set_target_properties(gneiss_serialization_json PROPERTIES EXPORT_NAME serialization_json
  POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_serialization_json)
gneiss_target_output_directories(gneiss_serialization_json)
