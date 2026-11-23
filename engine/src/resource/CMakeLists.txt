# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
add_library(gneiss_resource
  asset_file.cpp
  asset_uri.cpp
  builtin_resource_registration.cpp
  input/action_map_validation.cpp
  material.cpp
  material/material_description.cpp
  mesh.cpp
  mesh/mesh_binary.cpp
  mesh/mesh_description.cpp
  scene/prefab_property_data.cpp
  scene/scene_record.cpp
  scene/scene_validation.cpp
  texture.cpp
  texture/texture_binary.cpp
  texture/texture_description.cpp
  texture/texture_ktx2.cpp)
add_library(gneiss::resource ALIAS gneiss_resource)
target_link_libraries(gneiss_resource PUBLIC gneiss_serialization_json
  PRIVATE gneiss_libassert "$<BUILD_INTERFACE:gneiss_yyjson>")
target_include_directories(gneiss_resource PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_resource PRIVATE GNEISS_BUILDING_RESOURCE
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_RESOURCE_STATIC_DEFINE>)
set_target_properties(gneiss_resource PROPERTIES EXPORT_NAME resource POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
gneiss_target_compile_warnings(gneiss_resource)
gneiss_target_output_directories(gneiss_resource)
