# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_c_version_test c_version_test.c)
target_link_libraries(gneiss_c_version_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_version_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_version_test)
gneiss_target_output_directories(gneiss_c_version_test)
add_test(NAME gneiss.c_version COMMAND gneiss_c_version_test)

add_executable(gneiss_cpp_version_test cpp_version_test.cpp)
target_link_libraries(gneiss_cpp_version_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_version_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_version_test)
gneiss_target_output_directories(gneiss_cpp_version_test)
add_test(NAME gneiss.cpp_version COMMAND gneiss_cpp_version_test)

add_executable(gneiss_c_result_test c_result_test.c)
target_link_libraries(gneiss_c_result_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_result_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_result_test)
gneiss_target_output_directories(gneiss_c_result_test)
add_test(NAME gneiss.c_result COMMAND gneiss_c_result_test)

add_executable(gneiss_cpp_result_test cpp_result_test.cpp)
target_link_libraries(gneiss_cpp_result_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_result_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_result_test)
gneiss_target_output_directories(gneiss_cpp_result_test)
add_test(NAME gneiss.cpp_result COMMAND gneiss_cpp_result_test)

add_executable(gneiss_c_render_test c_render_test.c)
target_link_libraries(gneiss_c_render_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_render_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_render_test)
gneiss_target_output_directories(gneiss_c_render_test)
add_test(NAME gneiss.c_render COMMAND gneiss_c_render_test)

add_executable(
  gneiss_render_resource_service_test
  render/render_resource_service_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
)
target_include_directories(gneiss_render_resource_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_render_resource_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_render_resource_service_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_render_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_resource_service_test)
gneiss_target_output_directories(gneiss_render_resource_service_test)
add_test(NAME gneiss.render_resource_service COMMAND gneiss_render_resource_service_test)

add_executable(
  gneiss_png_decoder_test
  render/png_decoder_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
)
target_include_directories(gneiss_png_decoder_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_png_decoder_test PRIVATE gneiss::gneiss gneiss_png)
target_compile_features(gneiss_png_decoder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_png_decoder_test)
gneiss_target_output_directories(gneiss_png_decoder_test)
add_test(NAME gneiss.png_decoder COMMAND gneiss_png_decoder_test)

add_executable(gneiss_c_asset_test c_asset_test.c)
target_link_libraries(gneiss_c_asset_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_asset_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_asset_test)
gneiss_target_output_directories(gneiss_c_asset_test)
add_test(NAME gneiss.c_asset COMMAND gneiss_c_asset_test)

add_library(
  gneiss_c_header_test OBJECT headers/application_h.c headers/asset_h.c headers/entity_h.c headers/gneiss_h.c headers/input_h.c
                              headers/render_h.c headers/result_h.c headers/rid_h.c headers/scene_h.c headers/world_h.c
)
target_link_libraries(gneiss_c_header_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_c_header_test PRIVATE c_std_11)
gneiss_target_compile_warnings(gneiss_c_header_test)

add_library(
  gneiss_cpp_header_test OBJECT headers/application_hpp.cpp headers/asset_hpp.cpp headers/entity_hpp.cpp headers/gneiss_hpp.cpp
                                headers/input_hpp.cpp headers/render_hpp.cpp headers/result_hpp.cpp headers/rid_hpp.cpp
                                headers/scene_hpp.cpp headers/world_hpp.cpp
)
target_link_libraries(gneiss_cpp_header_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_header_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_header_test)

add_executable(gneiss_rid_table_test core/rid_table_test.cpp)
target_include_directories(gneiss_rid_table_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_rid_table_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_rid_table_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_rid_table_test)
gneiss_target_output_directories(gneiss_rid_table_test)
add_test(NAME gneiss.rid_table COMMAND gneiss_rid_table_test)

add_executable(
  gneiss_service_registry_test
  core/service_registry_test.cpp
  "${PROJECT_SOURCE_DIR}/src/core/service_registry.cpp"
)
target_include_directories(gneiss_service_registry_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_service_registry_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_service_registry_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_service_registry_test)
gneiss_target_output_directories(gneiss_service_registry_test)
add_test(NAME gneiss.service_registry COMMAND gneiss_service_registry_test)

add_executable(
  gneiss_input_service_test
  input/input_service_test.cpp
  "${PROJECT_SOURCE_DIR}/src/input/input_service.cpp"
)
target_include_directories(gneiss_input_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_input_service_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_input_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_input_service_test)
gneiss_target_output_directories(gneiss_input_service_test)
add_test(NAME gneiss.input_service COMMAND gneiss_input_service_test)

add_executable(
  gneiss_action_map_test
  input/action_map_test.cpp
  "${PROJECT_SOURCE_DIR}/src/input/action_map.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
)
target_include_directories(gneiss_action_map_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_action_map_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_action_map_test PRIVATE gneiss::gneiss gneiss_yyjson)
target_compile_features(gneiss_action_map_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_action_map_test)
gneiss_target_output_directories(gneiss_action_map_test)
add_test(NAME gneiss.action_map COMMAND gneiss_action_map_test)

add_executable(gneiss_c_world_test c_world_test.c)
target_link_libraries(gneiss_c_world_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_world_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_world_test)
gneiss_target_output_directories(gneiss_c_world_test)
add_test(NAME gneiss.c_world COMMAND gneiss_c_world_test)

add_executable(gneiss_cpp_world_test cpp_world_test.cpp)
target_link_libraries(gneiss_cpp_world_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_world_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_world_test)
gneiss_target_output_directories(gneiss_cpp_world_test)
add_test(NAME gneiss.cpp_world COMMAND gneiss_cpp_world_test)

add_executable(gneiss_c_scene_test c_scene_test.c)
target_link_libraries(gneiss_c_scene_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_scene_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_scene_test)
gneiss_target_output_directories(gneiss_c_scene_test)
add_test(NAME gneiss.c_scene COMMAND gneiss_c_scene_test)

add_executable(gneiss_cpp_scene_test cpp_scene_test.cpp)
target_link_libraries(gneiss_cpp_scene_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_scene_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_scene_test)
gneiss_target_output_directories(gneiss_cpp_scene_test)
add_test(NAME gneiss.cpp_scene COMMAND gneiss_cpp_scene_test)

add_executable(
  gneiss_scene_tree_transform_test
  scene/scene_tree_transform_test.cpp
  "${PROJECT_SOURCE_DIR}/src/scene/scene_tree.cpp"
)
target_include_directories(gneiss_scene_tree_transform_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_scene_tree_transform_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_scene_tree_transform_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_tree_transform_test)
gneiss_target_output_directories(gneiss_scene_tree_transform_test)
add_test(NAME gneiss.scene_tree_transform COMMAND gneiss_scene_tree_transform_test)

add_executable(gneiss_c_application_test c_application_test.c)
target_link_libraries(gneiss_c_application_test PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_application_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_application_test)
if(NOT GNEISS_ENABLE_GRANIT_PLATFORM)
  target_compile_definitions(gneiss_c_application_test PRIVATE GNEISS_TEST_NO_GRANIT_PLATFORM)
endif()
target_compile_definitions(
  gneiss_c_application_test PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
)
gneiss_target_output_directories(gneiss_c_application_test)
add_test(NAME gneiss.c_application COMMAND gneiss_c_application_test)

add_executable(gneiss_cpp_application_test cpp_application_test.cpp)
target_link_libraries(gneiss_cpp_application_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_application_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_application_test)
gneiss_target_output_directories(gneiss_cpp_application_test)
add_test(NAME gneiss.cpp_application COMMAND gneiss_cpp_application_test)

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  add_executable(gneiss_granit_platform_smoke_test granit_platform_smoke_test.cpp)
  target_link_libraries(gneiss_granit_platform_smoke_test PRIVATE gneiss::gneiss)
  target_compile_features(gneiss_granit_platform_smoke_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_granit_platform_smoke_test)
  gneiss_target_output_directories(gneiss_granit_platform_smoke_test)
  add_test(NAME gneiss.granit_platform_smoke COMMAND gneiss_granit_platform_smoke_test)
  set_tests_properties(gneiss.granit_platform_smoke PROPERTIES TIMEOUT 30)
endif()

add_executable(
  gneiss_world_state_test
  world/world_state_test.cpp
  "${PROJECT_SOURCE_DIR}/src/scene/scene_tree.cpp"
  "${PROJECT_SOURCE_DIR}/src/world/system_scheduler.cpp"
)
target_include_directories(gneiss_world_state_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_world_state_test PRIVATE gneiss::gneiss EnTT::EnTT)
target_compile_features(gneiss_world_state_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_world_state_test)
gneiss_target_output_directories(gneiss_world_state_test)
add_test(NAME gneiss.world_state COMMAND gneiss_world_state_test)

add_executable(
  gneiss_render_snapshot_test
  world/render_snapshot_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/camera_math.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_tree.cpp"
  "${PROJECT_SOURCE_DIR}/src/world/render_snapshot.cpp"
)
target_include_directories(gneiss_render_snapshot_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_render_snapshot_test PRIVATE gneiss::gneiss EnTT::EnTT)
target_compile_features(gneiss_render_snapshot_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_snapshot_test)
gneiss_target_output_directories(gneiss_render_snapshot_test)
add_test(NAME gneiss.render_snapshot COMMAND gneiss_render_snapshot_test)

add_executable(
  gneiss_camera_math_test
  render/camera_math_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/camera_math.cpp"
)
target_include_directories(gneiss_camera_math_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_camera_math_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_camera_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_camera_math_test)
gneiss_target_output_directories(gneiss_camera_math_test)
add_test(NAME gneiss.camera_math COMMAND gneiss_camera_math_test)

add_executable(gneiss_json_format_test serialization/json_format_test.cpp)
target_link_libraries(gneiss_json_format_test PRIVATE gneiss_yyjson)
target_compile_features(gneiss_json_format_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_json_format_test)
gneiss_target_output_directories(gneiss_json_format_test)
add_test(NAME gneiss.json_format COMMAND gneiss_json_format_test)

add_executable(
  gneiss_scene_description_test
  serialization/scene_description_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_description.cpp"
)
target_include_directories(gneiss_scene_description_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_scene_description_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_scene_description_test PRIVATE gneiss::gneiss gneiss_yyjson)
target_compile_features(gneiss_scene_description_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_description_test)
gneiss_target_output_directories(gneiss_scene_description_test)
add_test(NAME gneiss.scene_description COMMAND gneiss_scene_description_test)

add_executable(
  gneiss_resource_service_test
  asset/resource_service_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/native_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
)
target_include_directories(gneiss_resource_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_resource_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_resource_service_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_resource_service_test)
gneiss_target_output_directories(gneiss_resource_service_test)
add_test(NAME gneiss.resource_service COMMAND gneiss_resource_service_test)

add_executable(
  gneiss_render_asset_loader_test
  render/render_asset_loader_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
)
target_include_directories(gneiss_render_asset_loader_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_render_asset_loader_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(
  gneiss_render_asset_loader_test PRIVATE gneiss::gneiss gneiss_yyjson gneiss_png
)
target_compile_features(gneiss_render_asset_loader_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_asset_loader_test)
gneiss_target_output_directories(gneiss_render_asset_loader_test)
add_test(NAME gneiss.render_asset_loader COMMAND gneiss_render_asset_loader_test)

add_executable(gneiss_scene_instance_test scene/scene_instance_test.cpp)
target_link_libraries(gneiss_scene_instance_test PRIVATE gneiss::gneiss)
target_compile_definitions(
  gneiss_scene_instance_test
  PRIVATE
    GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
    GNEISS_TEST_FAILURE_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data"
)
target_compile_features(gneiss_scene_instance_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_instance_test)
gneiss_target_output_directories(gneiss_scene_instance_test)
add_test(NAME gneiss.scene_instance COMMAND gneiss_scene_instance_test)

if(TARGET gneiss_version_example)
  add_test(NAME gneiss.version_example COMMAND gneiss_version_example)
  set_tests_properties(
    gneiss.version_example
    PROPERTIES PASS_REGULAR_EXPRESSION "gneiss ${PROJECT_VERSION_MAJOR}\\.${PROJECT_VERSION_MINOR}\\.${PROJECT_VERSION_PATCH}"
  )
endif()

if(PROJECT_IS_TOP_LEVEL AND NOT GNEISS_ENABLE_GRANIT_PLATFORM)
  add_test(
    NAME gneiss.installed_consumer
    COMMAND
      "${CMAKE_COMMAND}"
      "-DGNEISS_BUILD_DIR=${PROJECT_BINARY_DIR}"
      "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DGNEISS_GENERATOR=${CMAKE_GENERATOR}"
      "-DGNEISS_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGNEISS_CONFIG=$<CONFIG>"
      "-DGNEISS_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DGNEISS_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DGNEISS_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_installed_package.cmake"
  )
endif()
