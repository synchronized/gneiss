# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_task_scheduler_test core/task_scheduler_test.cpp)
target_link_libraries(gneiss_task_scheduler_test PRIVATE gneiss_tasks)
gneiss_target_compile_warnings(gneiss_task_scheduler_test)
gneiss_target_output_directories(gneiss_task_scheduler_test)
add_test(NAME gneiss.task-scheduler COMMAND gneiss_task_scheduler_test)
set_tests_properties(gneiss.task-scheduler PROPERTIES TIMEOUT 30)

add_executable(gneiss_c_version_test c_version_test.c)
target_link_libraries(gneiss_c_version_test PRIVATE gneiss::engine)
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

add_test(
  NAME gneiss.api_stability_manifest
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGNEISS_ABI_BASELINE=${PROJECT_SOURCE_DIR}/abi/baseline/gneiss-1.0.0-c.txt"
    "-DGNEISS_API_STABILITY_MANIFEST=${PROJECT_SOURCE_DIR}/abi/api-stability.txt"
    "-DGNEISS_PUBLIC_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_api_stability.cmake"
)

add_test(
  NAME gneiss.granit_version_cache
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_granit_version_cache.cmake"
)

add_executable(gneiss_1_0_abi_consumer compat/1_0/consumer.c)
target_include_directories(gneiss_1_0_abi_consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat/1_0")
target_link_libraries(gneiss_1_0_abi_consumer PRIVATE gneiss::engine)
set_target_properties(
  gneiss_1_0_abi_consumer
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_1_0_abi_consumer)
gneiss_target_output_directories(gneiss_1_0_abi_consumer)
add_test(NAME gneiss.1_0_abi_consumer COMMAND gneiss_1_0_abi_consumer)

add_executable(gneiss_0_9_abi_consumer compat/0_9/consumer.c)
target_include_directories(gneiss_0_9_abi_consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat/0_9")
target_link_libraries(gneiss_0_9_abi_consumer PRIVATE gneiss::engine)
set_target_properties(
  gneiss_0_9_abi_consumer
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_0_9_abi_consumer)
gneiss_target_output_directories(gneiss_0_9_abi_consumer)
add_test(NAME gneiss.0_9_abi_consumer COMMAND gneiss_0_9_abi_consumer)

add_executable(gneiss_cpp_version_test cpp_version_test.cpp)
target_link_libraries(gneiss_cpp_version_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_version_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_version_test)
gneiss_target_output_directories(gneiss_cpp_version_test)
add_test(NAME gneiss.cpp_version COMMAND gneiss_cpp_version_test)

add_executable(gneiss_c_result_test c_result_test.c)
target_link_libraries(gneiss_c_result_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_cpp_result_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_result_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_result_test)
gneiss_target_output_directories(gneiss_cpp_result_test)
add_test(NAME gneiss.cpp_result COMMAND gneiss_cpp_result_test)

add_executable(gneiss_c_game_module_test c_game_module_test.c)
target_link_libraries(gneiss_c_game_module_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_game_module_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_game_module_test)
gneiss_target_output_directories(gneiss_c_game_module_test)
add_test(NAME gneiss.c_game_module COMMAND gneiss_c_game_module_test)

add_executable(gneiss_cpp_game_module_test cpp_game_module_test.cpp)
target_link_libraries(gneiss_cpp_game_module_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_game_module_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_game_module_test)
gneiss_target_output_directories(gneiss_cpp_game_module_test)
add_test(NAME gneiss.cpp_game_module COMMAND gneiss_cpp_game_module_test)

add_executable(gneiss_c_log_test c_log_test.c)
target_link_libraries(gneiss_c_log_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_log_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_log_test)
gneiss_target_output_directories(gneiss_c_log_test)
add_test(NAME gneiss.c_log COMMAND gneiss_c_log_test)

add_executable(gneiss_cpp_log_test cpp_log_test.cpp)
target_link_libraries(gneiss_cpp_log_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_log_test)
gneiss_target_output_directories(gneiss_cpp_log_test)
add_test(NAME gneiss.cpp_log COMMAND gneiss_cpp_log_test)

add_executable(gneiss_application_log_test application_log_test.cpp)
target_link_libraries(gneiss_application_log_test PRIVATE gneiss::engine)
target_compile_features(gneiss_application_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_application_log_test)
gneiss_target_output_directories(gneiss_application_log_test)
add_test(NAME gneiss.application_log COMMAND gneiss_application_log_test)

add_executable(gneiss_diagnostic_log_test diagnostic_log_test.cpp)
target_link_libraries(gneiss_diagnostic_log_test PRIVATE gneiss::engine)
target_compile_features(gneiss_diagnostic_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_diagnostic_log_test)
gneiss_target_output_directories(gneiss_diagnostic_log_test)
add_test(NAME gneiss.diagnostic_log COMMAND gneiss_diagnostic_log_test)

add_executable(
  gneiss_log_dispatcher_test
  log/log_dispatcher_test.cpp
  "${PROJECT_SOURCE_DIR}/src/log/log_dispatcher.cpp"
)
target_include_directories(gneiss_log_dispatcher_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_log_dispatcher_test PRIVATE gneiss::engine)
target_compile_features(gneiss_log_dispatcher_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_log_dispatcher_test)
gneiss_target_output_directories(gneiss_log_dispatcher_test)
add_test(NAME gneiss.log_dispatcher COMMAND gneiss_log_dispatcher_test)

add_executable(gneiss_game_context_test game_context_test.cpp)
target_include_directories(gneiss_game_context_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_game_context_test PRIVATE gneiss::engine)
target_compile_features(gneiss_game_context_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_game_context_test)
gneiss_target_output_directories(gneiss_game_context_test)
add_test(NAME gneiss.game_context COMMAND gneiss_game_context_test)

add_executable(gneiss_c_reflection_test c_reflection_test.c)
target_link_libraries(gneiss_c_reflection_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_reflection_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_reflection_test)
gneiss_target_output_directories(gneiss_c_reflection_test)
add_test(NAME gneiss.c_reflection COMMAND gneiss_c_reflection_test)

add_executable(gneiss_cpp_reflection_test cpp_reflection_test.cpp)
target_link_libraries(gneiss_cpp_reflection_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_reflection_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_reflection_test)
gneiss_target_output_directories(gneiss_cpp_reflection_test)
add_test(NAME gneiss.cpp_reflection COMMAND gneiss_cpp_reflection_test)

add_executable(gneiss_property_access_test reflection/property_access_test.cpp)
target_link_libraries(gneiss_property_access_test PRIVATE gneiss::engine)
target_compile_features(gneiss_property_access_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_property_access_test)
gneiss_target_output_directories(gneiss_property_access_test)
add_test(NAME gneiss.property_access COMMAND gneiss_property_access_test)

add_executable(gneiss_c_property_access_test c_property_access_test.c)
target_link_libraries(gneiss_c_property_access_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_property_access_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_property_access_test)
gneiss_target_output_directories(gneiss_c_property_access_test)
add_test(NAME gneiss.c_property_access COMMAND gneiss_c_property_access_test)

add_executable(gneiss_c_render_test c_render_test.c)
target_link_libraries(gneiss_c_render_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_render_resource_service_test PRIVATE gneiss::engine)
target_compile_features(gneiss_render_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_resource_service_test)
gneiss_target_output_directories(gneiss_render_resource_service_test)
add_test(NAME gneiss.render_resource_service COMMAND gneiss_render_resource_service_test)

add_executable(
  gneiss_pbr_material_resource_test
  render/pbr_material_resource_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
)
target_include_directories(gneiss_pbr_material_resource_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_pbr_material_resource_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_pbr_material_resource_test PRIVATE gneiss::engine)
target_compile_features(gneiss_pbr_material_resource_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_pbr_material_resource_test)
gneiss_target_output_directories(gneiss_pbr_material_resource_test)
add_test(NAME gneiss.pbr-material-resource COMMAND gneiss_pbr_material_resource_test)

add_executable(
  gneiss_ui_draw_list_test
  render/ui_draw_list_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/ui_draw_list.cpp"
)
target_include_directories(gneiss_ui_draw_list_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_ui_draw_list_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_ui_draw_list_test PRIVATE gneiss::engine)
target_compile_features(gneiss_ui_draw_list_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_ui_draw_list_test)
gneiss_target_output_directories(gneiss_ui_draw_list_test)
add_test(NAME gneiss.ui_draw_list COMMAND gneiss_ui_draw_list_test)

add_executable(
  gneiss_debug_draw_list_test
  render/debug_draw_list_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/debug_draw_list.cpp"
)
target_include_directories(gneiss_debug_draw_list_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_debug_draw_list_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_debug_draw_list_test PRIVATE gneiss::engine)
target_compile_features(gneiss_debug_draw_list_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_debug_draw_list_test)
gneiss_target_output_directories(gneiss_debug_draw_list_test)
add_test(NAME gneiss.debug_draw_list COMMAND gneiss_debug_draw_list_test)

add_executable(
  gneiss_png_decoder_test
  render/png_decoder_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
)
target_include_directories(gneiss_png_decoder_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_png_decoder_test PRIVATE gneiss::engine gneiss_png)
target_compile_features(gneiss_png_decoder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_png_decoder_test)
gneiss_target_output_directories(gneiss_png_decoder_test)
add_test(NAME gneiss.png_decoder COMMAND gneiss_png_decoder_test)

add_executable(gneiss_c_asset_test c_asset_test.c)
target_link_libraries(gneiss_c_asset_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_asset_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_asset_test)
gneiss_target_output_directories(gneiss_c_asset_test)
add_test(NAME gneiss.c_asset COMMAND gneiss_c_asset_test)

add_library(
  gneiss_c_header_test OBJECT headers/application_h.c headers/asset_h.c headers/entity_h.c headers/game_module_h.c
                              headers/gneiss_h.c headers/input_h.c headers/log_h.c headers/reflection_h.c
                              headers/render_h.c headers/result_h.c headers/rid_h.c headers/scene_h.c headers/world_h.c
)
target_link_libraries(gneiss_c_header_test PRIVATE gneiss::engine)
target_compile_features(gneiss_c_header_test PRIVATE c_std_11)
gneiss_target_compile_warnings(gneiss_c_header_test)

add_library(
  gneiss_cpp_header_test OBJECT headers/application_hpp.cpp headers/asset_hpp.cpp headers/entity_hpp.cpp
                                headers/game_module_hpp.cpp headers/gneiss_hpp.cpp headers/input_hpp.cpp headers/log_hpp.cpp
                                headers/reflection_hpp.cpp headers/render_hpp.cpp headers/result_hpp.cpp headers/rid_hpp.cpp
                                headers/scene_hpp.cpp headers/world_hpp.cpp
)
target_link_libraries(gneiss_cpp_header_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_header_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_header_test)

add_executable(gneiss_rid_table_test core/rid_table_test.cpp)
target_include_directories(gneiss_rid_table_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_rid_table_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_service_registry_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_input_service_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_action_map_test PRIVATE gneiss::engine gneiss_yyjson)
target_compile_features(gneiss_action_map_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_action_map_test)
gneiss_target_output_directories(gneiss_action_map_test)
add_test(NAME gneiss.action_map COMMAND gneiss_action_map_test)

add_executable(gneiss_c_world_test c_world_test.c)
target_link_libraries(gneiss_c_world_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_cpp_world_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_world_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_world_test)
gneiss_target_output_directories(gneiss_cpp_world_test)
add_test(NAME gneiss.cpp_world COMMAND gneiss_cpp_world_test)

add_executable(gneiss_c_scene_test c_scene_test.c)
target_link_libraries(gneiss_c_scene_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_scene_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_scene_test)
gneiss_target_output_directories(gneiss_c_scene_test)
add_test(NAME gneiss.c_scene COMMAND gneiss_c_scene_test)

add_executable(gneiss_cpp_scene_test cpp_scene_test.cpp)
target_link_libraries(gneiss_cpp_scene_test PRIVATE gneiss::engine)
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
target_link_libraries(gneiss_scene_tree_transform_test PRIVATE gneiss::engine)
target_compile_features(gneiss_scene_tree_transform_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_tree_transform_test)
gneiss_target_output_directories(gneiss_scene_tree_transform_test)
add_test(NAME gneiss.scene_tree_transform COMMAND gneiss_scene_tree_transform_test)

add_executable(gneiss_c_application_test c_application_test.c)
target_link_libraries(gneiss_c_application_test PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_application_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)
gneiss_target_compile_warnings(gneiss_c_application_test)
if(NOT GNEISS_ENABLE_GRANIT_PLATFORM)
  target_compile_definitions(gneiss_c_application_test PRIVATE GNEISS_TEST_NO_GRANIT_PLATFORM)
endif()
target_compile_definitions(
  gneiss_c_application_test PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
gneiss_target_output_directories(gneiss_c_application_test)
add_test(NAME gneiss.c_application COMMAND gneiss_c_application_test)

add_executable(gneiss_cpp_application_test cpp_application_test.cpp)
target_link_libraries(gneiss_cpp_application_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_application_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_application_test)
gneiss_target_output_directories(gneiss_cpp_application_test)
add_test(NAME gneiss.cpp_application COMMAND gneiss_cpp_application_test)

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  add_executable(
    gneiss_render_executor_test
    render/render_executor_test.cpp
    "${PROJECT_SOURCE_DIR}/src/render/render_executor.cpp"
  )
  target_include_directories(gneiss_render_executor_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_definitions(gneiss_render_executor_test PRIVATE GNEISS_BUILDING_LIBRARY)
  target_link_libraries(gneiss_render_executor_test PRIVATE gneiss::engine)
  target_compile_features(gneiss_render_executor_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_render_executor_test)
  gneiss_target_output_directories(gneiss_render_executor_test)
  add_test(NAME gneiss.render_executor COMMAND gneiss_render_executor_test)

  add_executable(
    gneiss_render_frame_packet_test
    render/render_frame_packet_test.cpp
    "${PROJECT_SOURCE_DIR}/src/render/debug_draw_list.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/render_frame_packet.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
  )
  target_include_directories(gneiss_render_frame_packet_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_definitions(gneiss_render_frame_packet_test PRIVATE GNEISS_BUILDING_LIBRARY)
  target_link_libraries(gneiss_render_frame_packet_test PRIVATE gneiss::engine)
  target_compile_features(gneiss_render_frame_packet_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_render_frame_packet_test)
  gneiss_target_output_directories(gneiss_render_frame_packet_test)
  add_test(NAME gneiss.render_frame_packet COMMAND gneiss_render_frame_packet_test)

  add_executable(
    gneiss_pbr_shader_resolver_test
    render/pbr_shader_resolver_test.cpp
    "${PROJECT_SOURCE_DIR}/src/render/granit/pbr_shader_resolver.cpp"
  )
  target_include_directories(
    gneiss_pbr_shader_resolver_test
    PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_BINARY_DIR}/src/generated/pbr"
  )
  target_compile_definitions(
    gneiss_pbr_shader_resolver_test
    PRIVATE
      GNEISS_TEST_GRANIT_PBR_ASSET_DIR="${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/libraries"
      GNEISS_TEST_GRANIT_PBR_MATERIAL="${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/materials/pbr_standard.grmat"
  )
  target_link_libraries(gneiss_pbr_shader_resolver_test PRIVATE granit::render_pipeline)
  target_compile_features(gneiss_pbr_shader_resolver_test PRIVATE cxx_std_20)
  add_dependencies(gneiss_pbr_shader_resolver_test gneiss_engine)
  gneiss_target_compile_warnings(gneiss_pbr_shader_resolver_test)
  gneiss_target_output_directories(gneiss_pbr_shader_resolver_test)
  add_test(NAME gneiss.pbr_shader_resolver COMMAND gneiss_pbr_shader_resolver_test)

  add_executable(gneiss_granit_platform_smoke_test granit_platform_smoke_test.cpp)
  target_link_libraries(gneiss_granit_platform_smoke_test PRIVATE gneiss::engine)
  if(WIN32)
    target_link_libraries(gneiss_granit_platform_smoke_test PRIVATE user32)
  endif()
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
target_link_libraries(gneiss_world_state_test PRIVATE gneiss::engine EnTT::EnTT)
target_compile_features(gneiss_world_state_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_world_state_test)
gneiss_target_output_directories(gneiss_world_state_test)
add_test(NAME gneiss.world_state COMMAND gneiss_world_state_test)

add_executable(gneiss_component_reflection_test world/component_reflection_test.cpp)
target_link_libraries(gneiss_component_reflection_test PRIVATE gneiss::engine)
target_compile_features(gneiss_component_reflection_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_component_reflection_test)
gneiss_target_output_directories(gneiss_component_reflection_test)
add_test(NAME gneiss.component_reflection COMMAND gneiss_component_reflection_test)

add_executable(
  gneiss_render_snapshot_test
  world/render_snapshot_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/camera_math.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_tree.cpp"
  "${PROJECT_SOURCE_DIR}/src/world/render_snapshot.cpp"
)
target_include_directories(gneiss_render_snapshot_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_render_snapshot_test PRIVATE gneiss::engine EnTT::EnTT)
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
target_link_libraries(gneiss_camera_math_test PRIVATE gneiss::engine)
target_compile_features(gneiss_camera_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_camera_math_test)
gneiss_target_output_directories(gneiss_camera_math_test)
add_test(NAME gneiss.camera_math COMMAND gneiss_camera_math_test)

add_executable(
  gneiss_normal_math_test
  render/normal_math_test.cpp
  "${PROJECT_SOURCE_DIR}/src/render/normal_math.cpp"
)
target_include_directories(gneiss_normal_math_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_normal_math_test PRIVATE gneiss::engine)
target_compile_features(gneiss_normal_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_normal_math_test)
gneiss_target_output_directories(gneiss_normal_math_test)
add_test(NAME gneiss.normal_math COMMAND gneiss_normal_math_test)

add_executable(gneiss_json_format_test serialization/json_format_test.cpp)
target_link_libraries(gneiss_json_format_test PRIVATE gneiss_yyjson)
target_compile_features(gneiss_json_format_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_json_format_test)
gneiss_target_output_directories(gneiss_json_format_test)
add_test(NAME gneiss.json_format COMMAND gneiss_json_format_test)

add_executable(gneiss_scene_load_application_test scene/scene_load_application_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp")
target_include_directories(gneiss_scene_load_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_scene_load_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_scene_load_application_test)
gneiss_target_output_directories(gneiss_scene_load_application_test)
add_test(NAME gneiss.scene-load-application COMMAND gneiss_scene_load_application_test)

add_executable(gneiss_scene_load_preparation_test
  scene/scene_load_preparation_test.cpp
  "${PROJECT_SOURCE_DIR}/src/scene/scene_load_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp")
target_include_directories(gneiss_scene_load_preparation_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_scene_load_preparation_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_scene_load_preparation_test PRIVATE gneiss::engine gneiss_yyjson)
target_compile_features(gneiss_scene_load_preparation_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_load_preparation_test)
gneiss_target_output_directories(gneiss_scene_load_preparation_test)
add_test(NAME gneiss.scene-load-preparation COMMAND gneiss_scene_load_preparation_test)

add_executable(
  gneiss_scene_description_test
  serialization/scene_description_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_description.cpp"
)
target_include_directories(gneiss_scene_description_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_scene_description_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_scene_description_test PRIVATE gneiss::engine gneiss_yyjson)
target_compile_features(gneiss_scene_description_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_description_test)
gneiss_target_output_directories(gneiss_scene_description_test)
add_test(NAME gneiss.scene_description COMMAND gneiss_scene_description_test)

add_executable(
  gneiss_prefab_description_test
  serialization/prefab_description_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_tree.cpp"
)
target_include_directories(gneiss_prefab_description_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_prefab_description_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_prefab_description_test PRIVATE gneiss::engine gneiss_yyjson)
target_compile_features(gneiss_prefab_description_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_description_test)
gneiss_target_output_directories(gneiss_prefab_description_test)
add_test(NAME gneiss.prefab_description COMMAND gneiss_prefab_description_test)

add_executable(
  gneiss_prefab_property_override_test
  serialization/prefab_property_override_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_description.cpp"
)
target_include_directories(gneiss_prefab_property_override_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_prefab_property_override_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_prefab_property_override_test PRIVATE gneiss::engine gneiss_yyjson)
target_compile_features(gneiss_prefab_property_override_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_property_override_test)
gneiss_target_output_directories(gneiss_prefab_property_override_test)
add_test(NAME gneiss.prefab_property_override COMMAND gneiss_prefab_property_override_test)

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
target_link_libraries(gneiss_resource_service_test PRIVATE gneiss::engine)
target_compile_features(gneiss_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_resource_service_test)
gneiss_target_output_directories(gneiss_resource_service_test)
add_test(NAME gneiss.resource_service COMMAND gneiss_resource_service_test)

find_package(Threads REQUIRED)
add_executable(gneiss_read_source_test
  asset/read_source_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/native_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp")
target_include_directories(gneiss_read_source_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(gneiss_read_source_test PRIVATE Threads::Threads)
target_compile_definitions(gneiss_read_source_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_compile_features(gneiss_read_source_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_read_source_test)
gneiss_target_output_directories(gneiss_read_source_test)
add_test(NAME gneiss.read_source COMMAND gneiss_read_source_test)

add_executable(
  gneiss_mesh_binary_test
  asset/mesh_binary_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
)
target_include_directories(gneiss_mesh_binary_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_mesh_binary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_mesh_binary_test)
gneiss_target_output_directories(gneiss_mesh_binary_test)
add_test(NAME gneiss.mesh_binary COMMAND gneiss_mesh_binary_test)

add_executable(
  gneiss_texture_ktx2_test
  asset/texture_ktx2_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp"
)
target_include_directories(gneiss_texture_ktx2_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_texture_ktx2_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_ktx2_test)
gneiss_target_output_directories(gneiss_texture_ktx2_test)
add_test(NAME gneiss.texture_ktx2 COMMAND gneiss_texture_ktx2_test)

add_executable(
  gneiss_texture_binary_test
  asset/texture_binary_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
)
target_include_directories(gneiss_texture_binary_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_texture_binary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_binary_test)
gneiss_target_output_directories(gneiss_texture_binary_test)
add_test(NAME gneiss.texture_binary COMMAND gneiss_texture_binary_test)

add_executable(gneiss_texture_container_test
  asset/texture_container_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_container.cpp")
target_include_directories(gneiss_texture_container_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(gneiss_texture_container_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_container_test)
gneiss_target_output_directories(gneiss_texture_container_test)
add_test(NAME gneiss.texture_container COMMAND gneiss_texture_container_test)

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  add_executable(
    gneiss_scene_projection_math_test
    render/scene_projection_math_test.cpp
    "${PROJECT_SOURCE_DIR}/src/render/granit/scene_projection_math.cpp"
  )
  target_include_directories(
    gneiss_scene_projection_math_test PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src"
  )
  target_link_libraries(gneiss_scene_projection_math_test PRIVATE granit::granit)
  target_compile_features(gneiss_scene_projection_math_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_scene_projection_math_test)
  gneiss_target_output_directories(gneiss_scene_projection_math_test)
  add_test(NAME gneiss.scene_projection_math COMMAND gneiss_scene_projection_math_test)
endif()

add_executable(
  gneiss_render_asset_loader_test
  render/render_asset_loader_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_container.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/source_revision_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
)
target_include_directories(gneiss_render_asset_loader_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_render_asset_loader_test PRIVATE GNEISS_BUILDING_LIBRARY)
if(GNEISS_ENABLE_GRANIT_PLATFORM)
  target_compile_definitions(gneiss_render_asset_loader_test PRIVATE GNEISS_HAS_GRANIT_PLATFORM)
endif()
target_link_libraries(
  gneiss_render_asset_loader_test PRIVATE gneiss::engine gneiss_yyjson gneiss_png granit::granit granit::asset_tools
)
target_compile_features(gneiss_render_asset_loader_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_asset_loader_test)
gneiss_target_output_directories(gneiss_render_asset_loader_test)
add_test(NAME gneiss.render_asset_loader COMMAND gneiss_render_asset_loader_test)

add_executable(
  gneiss_texture_load_service_test
  asset/texture_load_service_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/texture_load_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_container.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/source_revision_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
)
target_include_directories(gneiss_texture_load_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_texture_load_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
if(GNEISS_ENABLE_GRANIT_PLATFORM)
  target_compile_definitions(gneiss_texture_load_service_test PRIVATE GNEISS_HAS_GRANIT_PLATFORM)
endif()
target_link_libraries(
  gneiss_texture_load_service_test PRIVATE gneiss::engine gneiss_tasks gneiss_yyjson gneiss_png granit::granit granit::asset_tools
)
target_compile_features(gneiss_texture_load_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_load_service_test)
gneiss_target_output_directories(gneiss_texture_load_service_test)
add_test(NAME gneiss.texture-load-service COMMAND gneiss_texture_load_service_test)

add_executable(gneiss_texture_load_application_test asset/texture_load_application_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp")
target_include_directories(gneiss_texture_load_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_texture_load_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_texture_load_application_test)
gneiss_target_output_directories(gneiss_texture_load_application_test)
add_test(NAME gneiss.texture-load-application COMMAND gneiss_texture_load_application_test)
set_tests_properties(gneiss.texture-load-service gneiss.texture-load-application PROPERTIES TIMEOUT 30)

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  add_executable(gneiss_asset_pixel_test render/asset_pixel_test.cpp
    "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
    "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
    "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp")
  target_include_directories(gneiss_asset_pixel_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_asset_pixel_test PRIVATE gneiss::engine gneiss_tasks granit::asset_tools granit::granit)
  gneiss_target_compile_warnings(gneiss_asset_pixel_test)
  gneiss_target_output_directories(gneiss_asset_pixel_test)
  add_test(NAME gneiss.asset-pixel COMMAND gneiss_asset_pixel_test)
  add_test(NAME gneiss.pbr-negative-scale COMMAND gneiss_asset_pixel_test --probe-negative-scale)
  set_tests_properties(gneiss.asset-pixel PROPERTIES TIMEOUT 60)
  add_executable(gneiss_scene_load_baseline render/scene_load_baseline.cpp)
  target_include_directories(gneiss_scene_load_baseline PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_scene_load_baseline PRIVATE gneiss::engine gneiss_tasks)
  if(WIN32)
    target_link_libraries(gneiss_scene_load_baseline PRIVATE psapi)
  endif()
  gneiss_target_compile_warnings(gneiss_scene_load_baseline)
  gneiss_target_output_directories(gneiss_scene_load_baseline)
  add_executable(gneiss_texture_load_window_benchmark render/texture_load_window_benchmark.cpp)
  target_include_directories(gneiss_texture_load_window_benchmark PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_texture_load_window_benchmark PRIVATE gneiss::engine gneiss_tasks)
  if(WIN32)
    target_link_libraries(gneiss_texture_load_window_benchmark PRIVATE user32)
  endif()
  gneiss_target_compile_warnings(gneiss_texture_load_window_benchmark)
  gneiss_target_output_directories(gneiss_texture_load_window_benchmark)
endif()

add_executable(gneiss_scene_instance_test scene/scene_instance_test.cpp)
target_link_libraries(gneiss_scene_instance_test PRIVATE gneiss::engine)
target_compile_definitions(
  gneiss_scene_instance_test
  PRIVATE
    GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
    GNEISS_TEST_FAILURE_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data"
)
target_compile_features(gneiss_scene_instance_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_instance_test)
gneiss_target_output_directories(gneiss_scene_instance_test)
add_test(NAME gneiss.scene_instance COMMAND gneiss_scene_instance_test)

add_executable(gneiss_scene_structural_reload_test scene/scene_structural_reload_test.cpp)
target_link_libraries(gneiss_scene_structural_reload_test PRIVATE gneiss::engine)
target_include_directories(gneiss_scene_structural_reload_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
  gneiss_scene_structural_reload_test
  PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
target_compile_features(gneiss_scene_structural_reload_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_structural_reload_test)
gneiss_target_output_directories(gneiss_scene_structural_reload_test)
add_test(NAME gneiss.scene_structural_reload COMMAND gneiss_scene_structural_reload_test)

add_executable(
  gneiss_structural_diff_test
  scene/structural_diff_test.cpp
  "${PROJECT_SOURCE_DIR}/src/scene/structural_diff.cpp"
)
target_include_directories(
  gneiss_structural_diff_test PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include"
)
target_compile_features(gneiss_structural_diff_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_structural_diff_test)
gneiss_target_output_directories(gneiss_structural_diff_test)
add_test(NAME gneiss.structural_diff COMMAND gneiss_structural_diff_test)

add_executable(
  gneiss_prefab_runtime_instance_test
  scene/prefab_runtime_instance_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_container.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/source_revision_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/render_resource_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/prefab_runtime_instance.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_description.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/structural_diff.cpp"
)
target_include_directories(gneiss_prefab_runtime_instance_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_prefab_runtime_instance_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(
  gneiss_prefab_runtime_instance_test
  PRIVATE gneiss::engine gneiss_yyjson gneiss_png granit::granit
)
target_compile_features(gneiss_prefab_runtime_instance_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_runtime_instance_test)
gneiss_target_output_directories(gneiss_prefab_runtime_instance_test)
add_test(NAME gneiss.prefab_runtime_instance COMMAND gneiss_prefab_runtime_instance_test)

get_target_property(gneiss_scene_fixture_sources gneiss_prefab_runtime_instance_test SOURCES)
list(REMOVE_ITEM gneiss_scene_fixture_sources scene/prefab_runtime_instance_test.cpp)
add_executable(gneiss_scene_load_builder_test scene/scene_load_builder_test.cpp
  ${gneiss_scene_fixture_sources}
  "${PROJECT_SOURCE_DIR}/src/application/application_scene_state.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_instance_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_load_builder.cpp"
  "${PROJECT_SOURCE_DIR}/src/scene/scene_load_preparation.cpp")
target_include_directories(gneiss_scene_load_builder_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_scene_load_builder_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_scene_load_builder_test PRIVATE gneiss::engine gneiss_yyjson gneiss_png granit::granit)
target_compile_features(gneiss_scene_load_builder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_load_builder_test)
gneiss_target_output_directories(gneiss_scene_load_builder_test)
add_test(NAME gneiss.scene-load-builder COMMAND gneiss_scene_load_builder_test)

add_executable(gneiss_scene_prefab_instance_test scene/scene_prefab_instance_test.cpp)
target_link_libraries(gneiss_scene_prefab_instance_test PRIVATE gneiss::engine)
target_compile_definitions(
  gneiss_scene_prefab_instance_test
  PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
target_compile_features(gneiss_scene_prefab_instance_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_prefab_instance_test)
gneiss_target_output_directories(gneiss_scene_prefab_instance_test)
add_test(NAME gneiss.scene_prefab_instance COMMAND gneiss_scene_prefab_instance_test)

add_executable(gneiss_prefab_structural_reload_test scene/prefab_structural_reload_test.cpp)
target_link_libraries(gneiss_prefab_structural_reload_test PRIVATE gneiss::engine)
target_include_directories(gneiss_prefab_structural_reload_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
  gneiss_prefab_structural_reload_test
  PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
target_compile_features(gneiss_prefab_structural_reload_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_structural_reload_test)
gneiss_target_output_directories(gneiss_prefab_structural_reload_test)
add_test(NAME gneiss.prefab_structural_reload COMMAND gneiss_prefab_structural_reload_test)

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

if(PROJECT_IS_TOP_LEVEL AND GNEISS_ENABLE_GRANIT_PLATFORM)
  add_test(
    NAME gneiss.installed_stable_runtime_consumer
    COMMAND
      "${CMAKE_COMMAND}"
      "-DGNEISS_BUILD_DIR=${PROJECT_BINARY_DIR}"
      "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DGNEISS_GENERATOR=${CMAKE_GENERATOR}"
      "-DGNEISS_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGNEISS_CONFIG=$<CONFIG>"
      "-DGNEISS_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DGNEISS_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DGNEISS_DEPENDENCY_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
      "-DGNEISS_GRANIT_BUILD_DIR=${GNEISS_GRANIT_FETCHED_BUILD_DIR}"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_installed_stable_runtime.cmake"
  )
  set_tests_properties(gneiss.installed_stable_runtime_consumer PROPERTIES TIMEOUT 120)
endif()

add_executable(gneiss_source_revision_test
  asset/source_revision_file_system_test.cpp
  "${PROJECT_SOURCE_DIR}/src/asset/source_revision_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/virtual_file_system.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/sha256.cpp"
)
target_include_directories(gneiss_source_revision_test PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(gneiss_source_revision_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_source_revision_test)
gneiss_target_output_directories(gneiss_source_revision_test)
add_test(NAME gneiss.source-revision COMMAND gneiss_source_revision_test)
target_compile_definitions(gneiss_source_revision_test PRIVATE GNEISS_BUILDING_LIBRARY)
