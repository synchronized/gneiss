# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_profiling_disabled_test profiling_disabled_test.cpp)
target_include_directories(gneiss_profiling_disabled_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_profiling_disabled_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_profiling_disabled_test)
gneiss_target_output_directories(gneiss_profiling_disabled_test)
add_test(NAME gneiss.profiling-disabled COMMAND gneiss_profiling_disabled_test)

if(GNEISS_ENABLE_PROFILING AND GNEISS_TASK_THREADS)
  add_executable(gneiss_profiling_capture profiling_capture.cpp)
  target_link_libraries(gneiss_profiling_capture PRIVATE gneiss::engine gneiss_tasks gneiss_native_interface)
  gneiss_target_profiling(gneiss_profiling_capture)
  gneiss_target_compile_warnings(gneiss_profiling_capture)
  gneiss_target_output_directories(gneiss_profiling_capture)
  add_test(NAME gneiss.profiling-capture COMMAND gneiss_profiling_capture)
  set_tests_properties(gneiss.profiling-capture PROPERTIES TIMEOUT 15)
endif()

if(GNEISS_ENABLE_PROFILING AND WIN32)
  # 手动诊断夹具只链接 Tracy，不携带引擎、调度器或渲染器。
  add_executable(gneiss_filesystem_trace_probe asset/filesystem_trace_probe.cpp)
  target_include_directories(gneiss_filesystem_trace_probe PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_features(gneiss_filesystem_trace_probe PRIVATE cxx_std_20)
  gneiss_target_profiling(gneiss_filesystem_trace_probe)
  gneiss_target_compile_warnings(gneiss_filesystem_trace_probe)
  gneiss_target_output_directories(gneiss_filesystem_trace_probe)
endif()

if(NOT EMSCRIPTEN AND GNEISS_TASK_THREADS)
  add_executable(gneiss_resource_retirement_queue_test core/resource_retirement_queue_test.cpp)
  target_include_directories(gneiss_resource_retirement_queue_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_features(gneiss_resource_retirement_queue_test PRIVATE cxx_std_20)
  target_link_libraries(gneiss_resource_retirement_queue_test PRIVATE Threads::Threads)
  gneiss_target_compile_warnings(gneiss_resource_retirement_queue_test)
  gneiss_target_output_directories(gneiss_resource_retirement_queue_test)
  add_test(NAME gneiss.resource-retirement-queue COMMAND gneiss_resource_retirement_queue_test)
  set_tests_properties(gneiss.resource-retirement-queue PROPERTIES TIMEOUT 30)

  add_executable(gneiss_main_loop_wakeup_test runtime/main_loop_wakeup_test.cpp)
  target_include_directories(gneiss_main_loop_wakeup_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_main_loop_wakeup_test PRIVATE gneiss_tasks Threads::Threads)
  gneiss_target_compile_warnings(gneiss_main_loop_wakeup_test)
  gneiss_target_output_directories(gneiss_main_loop_wakeup_test)
  add_test(NAME gneiss.main-loop-wakeup COMMAND gneiss_main_loop_wakeup_test)
  set_tests_properties(gneiss.main-loop-wakeup PROPERTIES TIMEOUT 30)
endif()

add_executable(gneiss_task_scheduler_test core/task_scheduler_test.cpp)
target_link_libraries(gneiss_task_scheduler_test PRIVATE gneiss_tasks)
gneiss_target_compile_warnings(gneiss_task_scheduler_test)
gneiss_target_output_directories(gneiss_task_scheduler_test)
add_test(NAME gneiss.task-scheduler COMMAND gneiss_task_scheduler_test)
set_tests_properties(gneiss.task-scheduler PROPERTIES TIMEOUT 30)


add_test(
  NAME gneiss.granit_version_cache
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_granit_version_cache.cmake"
)

add_executable(gneiss_cpp_version_test cpp_version_test.cpp)
target_link_libraries(gneiss_cpp_version_test PRIVATE gneiss::engine gneiss_value_interface)
target_include_directories(gneiss_cpp_version_test PRIVATE "${PROJECT_BINARY_DIR}/src/generated/include")
target_compile_features(gneiss_cpp_version_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_version_test)
gneiss_target_output_directories(gneiss_cpp_version_test)
add_test(NAME gneiss.cpp_version COMMAND gneiss_cpp_version_test)


















add_executable(gneiss_cpp_log_test cpp_log_test.cpp "${PROJECT_SOURCE_DIR}/src/engine/core/log/log.cpp")
target_include_directories(gneiss_cpp_log_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_cpp_log_test PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_log_test)
gneiss_target_output_directories(gneiss_cpp_log_test)
add_test(NAME gneiss.cpp_log COMMAND gneiss_cpp_log_test)

add_executable(gneiss_application_log_test application_log_test.cpp)
target_link_libraries(gneiss_application_log_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_application_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_application_log_test)
gneiss_target_output_directories(gneiss_application_log_test)
add_test(NAME gneiss.application_log COMMAND gneiss_application_log_test)

add_executable(gneiss_diagnostic_log_test diagnostic_log_test.cpp)
target_link_libraries(gneiss_diagnostic_log_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_diagnostic_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_diagnostic_log_test)
gneiss_target_output_directories(gneiss_diagnostic_log_test)
add_test(NAME gneiss.diagnostic_log COMMAND gneiss_diagnostic_log_test)

add_executable(
  gneiss_log_dispatcher_test
  log/log_dispatcher_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/core/log/log.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/log/log_dispatcher.cpp"
)
target_include_directories(gneiss_log_dispatcher_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_log_dispatcher_test PRIVATE gneiss_core_values Threads::Threads)
target_compile_features(gneiss_log_dispatcher_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_log_dispatcher_test)
gneiss_target_output_directories(gneiss_log_dispatcher_test)
add_test(NAME gneiss.log_dispatcher COMMAND gneiss_log_dispatcher_test)











add_executable(gneiss_property_access_test meta/property_access_test.cpp)
target_include_directories(gneiss_property_access_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_property_access_test PRIVATE gneiss::engine)
target_compile_features(gneiss_property_access_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_property_access_test)
gneiss_target_output_directories(gneiss_property_access_test)
add_test(NAME gneiss.property_access COMMAND gneiss_property_access_test)


add_executable(gneiss_native_render_lifecycle_test native_render_lifecycle_test.cpp)
target_link_libraries(gneiss_native_render_lifecycle_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_native_render_lifecycle_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_render_lifecycle_test)
gneiss_target_output_directories(gneiss_native_render_lifecycle_test)
add_test(NAME gneiss.native-render-lifecycle COMMAND gneiss_native_render_lifecycle_test)

add_executable(
  gneiss_render_resource_service_test
  render/render_resource_service_test.cpp
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
  "${PROJECT_SOURCE_DIR}/src/engine/function/render/ui_draw_list.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/function/render/debug_draw_list.cpp"
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
  asset/png_decoder_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/png_decoder.cpp"
)
target_include_directories(gneiss_png_decoder_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_png_decoder_test PRIVATE gneiss::engine gneiss_png)
target_compile_features(gneiss_png_decoder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_png_decoder_test)
gneiss_target_output_directories(gneiss_png_decoder_test)
add_test(NAME gneiss.png_decoder COMMAND gneiss_png_decoder_test)

add_executable(gneiss_asset_uri_test asset_uri_test.cpp)
target_link_libraries(gneiss_asset_uri_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_asset_uri_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_asset_uri_test)
gneiss_target_output_directories(gneiss_asset_uri_test)
add_test(NAME gneiss.asset_uri COMMAND gneiss_asset_uri_test)

add_library(
  gneiss_cpp_header_test OBJECT  headers/asset_hpp.cpp headers/entity_hpp.cpp
                                  headers/input_hpp.cpp headers/log_hpp.cpp
                                 headers/result_hpp.cpp headers/rid_hpp.cpp
                                headers/scene_hpp.cpp
)
target_link_libraries(gneiss_cpp_header_test PRIVATE gneiss::engine gneiss_native_interface)
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
  "${PROJECT_SOURCE_DIR}/src/engine/core/service_registry.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/input/action_map_validation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/input/input_service.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/input/action_map_validation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/input/action_map_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
)
target_include_directories(gneiss_action_map_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_action_map_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_action_map_test PRIVATE gneiss::engine gneiss_yyjson gneiss_tasks)
target_compile_features(gneiss_action_map_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_action_map_test)
gneiss_target_output_directories(gneiss_action_map_test)
add_test(NAME gneiss.action_map COMMAND gneiss_action_map_test)

add_executable(gneiss_native_world_lifecycle_test native_world_lifecycle_test.cpp)
target_link_libraries(gneiss_native_world_lifecycle_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_native_world_lifecycle_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_world_lifecycle_test)
gneiss_target_output_directories(gneiss_native_world_lifecycle_test)
add_test(NAME gneiss.native-world-lifecycle COMMAND gneiss_native_world_lifecycle_test)

add_executable(gneiss_native_scene_lifecycle_test native_scene_lifecycle_test.cpp)
target_link_libraries(gneiss_native_scene_lifecycle_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_native_scene_lifecycle_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_scene_lifecycle_test)
gneiss_target_output_directories(gneiss_native_scene_lifecycle_test)
add_test(NAME gneiss.native-scene-lifecycle COMMAND gneiss_native_scene_lifecycle_test)

add_executable(
  gneiss_scene_tree_transform_test
  scene/scene_tree_transform_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_tree.cpp"
)
target_include_directories(gneiss_scene_tree_transform_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_scene_tree_transform_test PRIVATE gneiss::engine)
target_compile_features(gneiss_scene_tree_transform_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_tree_transform_test)
gneiss_target_output_directories(gneiss_scene_tree_transform_test)
add_test(NAME gneiss.scene_tree_transform COMMAND gneiss_scene_tree_transform_test)

add_executable(gneiss_native_application_flow_test native_application_flow_test.cpp)
target_link_libraries(gneiss_native_application_flow_test PRIVATE gneiss::engine gneiss_native_interface gneiss_tasks)
target_compile_features(gneiss_native_application_flow_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_application_flow_test)
target_compile_definitions(
  gneiss_native_application_flow_test PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
gneiss_target_output_directories(gneiss_native_application_flow_test)
add_test(NAME gneiss.native-application-flow COMMAND gneiss_native_application_flow_test)

# 原生模块接口不依赖 Engine 的平台私有布局开关。
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  add_executable(gneiss_native_application_test native_application_test.cpp)
  target_include_directories(gneiss_native_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_native_application_test PRIVATE gneiss::engine Threads::Threads)
  gneiss_target_compile_warnings(gneiss_native_application_test)
  gneiss_target_output_directories(gneiss_native_application_test)
  add_test(NAME gneiss.native-application COMMAND gneiss_native_application_test)
endif()

add_executable(gneiss_cpp_input_test cpp_input_test.cpp)
target_link_libraries(gneiss_cpp_input_test PRIVATE gneiss::engine gneiss_native_interface gneiss_tasks)
target_compile_definitions(gneiss_cpp_input_test
  PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
target_compile_features(gneiss_cpp_input_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_input_test)
gneiss_target_output_directories(gneiss_cpp_input_test)
add_test(NAME gneiss.cpp_input COMMAND gneiss_cpp_input_test)

add_executable(
  gneiss_render_executor_test
  render/render_executor_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/render/render_executor.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/function/render/debug_draw_list.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/render/render_frame_packet.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/function/render/backend/granit/pbr_shader_resolver.cpp"
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
target_link_libraries(gneiss_granit_platform_smoke_test PRIVATE gneiss::engine gneiss_native_interface)
if(WIN32)
  target_link_libraries(gneiss_granit_platform_smoke_test PRIVATE user32)
endif()
target_compile_features(gneiss_granit_platform_smoke_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_granit_platform_smoke_test)
gneiss_target_output_directories(gneiss_granit_platform_smoke_test)
add_test(NAME gneiss.granit_platform_smoke COMMAND gneiss_granit_platform_smoke_test)
set_tests_properties(gneiss.granit_platform_smoke PROPERTIES TIMEOUT 30)

add_executable(
  gneiss_world_state_test
  world/world_state_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_tree.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/world/system_scheduler.cpp"
)
target_include_directories(gneiss_world_state_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_world_state_test PRIVATE gneiss::engine EnTT::EnTT)
target_compile_features(gneiss_world_state_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_world_state_test)
gneiss_target_output_directories(gneiss_world_state_test)
add_test(NAME gneiss.world_state COMMAND gneiss_world_state_test)

add_executable(gneiss_component_reflection_test world/component_reflection_test.cpp)
target_include_directories(gneiss_component_reflection_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_component_reflection_test PRIVATE gneiss::engine Threads::Threads)
target_compile_features(gneiss_component_reflection_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_component_reflection_test)
gneiss_target_output_directories(gneiss_component_reflection_test)
add_test(NAME gneiss.component_reflection COMMAND gneiss_component_reflection_test)

add_executable(
  gneiss_render_snapshot_test
  world/render_snapshot_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_tree.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/world/render_snapshot.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/core/math/transform.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp")
target_include_directories(gneiss_scene_load_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_scene_load_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_scene_load_application_test)
gneiss_target_output_directories(gneiss_scene_load_application_test)
add_test(NAME gneiss.scene-load-application COMMAND gneiss_scene_load_application_test)

add_executable(gneiss_scene_load_preparation_test
  scene/scene_load_preparation_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_load_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/scene_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_property_data.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
)
target_include_directories(gneiss_scene_load_preparation_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_scene_load_preparation_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_scene_load_preparation_test PRIVATE gneiss::engine gneiss_yyjson gneiss_tasks)
target_compile_features(gneiss_scene_load_preparation_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_load_preparation_test)
gneiss_target_output_directories(gneiss_scene_load_preparation_test)
add_test(NAME gneiss.scene-load-preparation COMMAND gneiss_scene_load_preparation_test)

add_executable(gneiss_prefab_preparation_test scene/prefab_preparation_test.cpp)
target_include_directories(gneiss_prefab_preparation_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_prefab_preparation_test PRIVATE GNEISS_TEST_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/data/assets")
target_link_libraries(gneiss_prefab_preparation_test PRIVATE gneiss::engine gneiss_tasks)
target_compile_features(gneiss_prefab_preparation_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_preparation_test)
gneiss_target_output_directories(gneiss_prefab_preparation_test)
add_test(NAME gneiss.prefab-preparation COMMAND gneiss_prefab_preparation_test)

add_executable(
  gneiss_scene_description_test
  serialization/scene_description_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_property_data.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/scene_codec.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_property_data.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/scene_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_tree.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_property_data.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/scene_codec.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/resource_cache.cpp"
)
target_include_directories(gneiss_resource_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_resource_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_resource_service_test PRIVATE gneiss::engine)
target_compile_features(gneiss_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_resource_service_test)
gneiss_target_output_directories(gneiss_resource_service_test)
add_test(NAME gneiss.resource_service COMMAND gneiss_resource_service_test)

find_package(Threads REQUIRED)
add_executable(gneiss_loop_timing_test loop_timing_test.cpp)
target_include_directories(gneiss_loop_timing_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_loop_timing_test PRIVATE Threads::Threads)
target_compile_features(gneiss_loop_timing_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_loop_timing_test)
gneiss_target_output_directories(gneiss_loop_timing_test)
add_test(NAME gneiss.loop_timing COMMAND gneiss_loop_timing_test)



add_executable(
  gneiss_mesh_binary_test
  asset/mesh_binary_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
)
target_include_directories(gneiss_mesh_binary_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_mesh_binary_test PRIVATE gneiss_math_dependency)
target_compile_features(gneiss_mesh_binary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_mesh_binary_test)
gneiss_target_output_directories(gneiss_mesh_binary_test)
add_test(NAME gneiss.mesh_binary COMMAND gneiss_mesh_binary_test)

add_executable(
  gneiss_texture_ktx2_test
  asset/texture_ktx2_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp"
)
target_include_directories(gneiss_texture_ktx2_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_texture_ktx2_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_ktx2_test)
gneiss_target_output_directories(gneiss_texture_ktx2_test)
add_test(NAME gneiss.texture_ktx2 COMMAND gneiss_texture_ktx2_test)

add_executable(
  gneiss_texture_binary_test
  asset/texture_binary_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_binary.cpp"
)
target_include_directories(gneiss_texture_binary_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_texture_binary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_binary_test)
gneiss_target_output_directories(gneiss_texture_binary_test)
add_test(NAME gneiss.texture_binary COMMAND gneiss_texture_binary_test)



add_executable(
  gneiss_scene_projection_math_test
  render/scene_projection_math_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/core/math/transform.cpp"
)
target_include_directories(
  gneiss_scene_projection_math_test PRIVATE "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(gneiss_scene_projection_math_test PRIVATE gneiss_math_dependency)
target_compile_features(gneiss_scene_projection_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_projection_math_test)
gneiss_target_output_directories(gneiss_scene_projection_math_test)
add_test(NAME gneiss.scene_projection_math COMMAND gneiss_scene_projection_math_test)

# CPU 准备单独链接：不得通过 Engine 或资源服务补齐依赖。
add_executable(gneiss_render_asset_preparation_test
  "${PROJECT_SOURCE_DIR}/src/engine/core/guid.cpp"
  asset/asset_preparation_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/asset_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/png_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
)
target_compile_definitions(gneiss_render_asset_preparation_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_include_directories(gneiss_render_asset_preparation_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_render_asset_preparation_test PRIVATE
  gneiss_core_values gneiss_yyjson gneiss_png granit::granit Threads::Threads)
target_compile_features(gneiss_render_asset_preparation_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_asset_preparation_test)
gneiss_target_output_directories(gneiss_render_asset_preparation_test)
add_test(NAME gneiss.render_asset_preparation COMMAND gneiss_render_asset_preparation_test)

add_executable(
  gneiss_render_asset_loader_test
  render/render_asset_loader_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/detail/texture_load_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/detail/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/asset_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/png_decoder.cpp"
)
target_include_directories(gneiss_render_asset_loader_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_render_asset_loader_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(
  gneiss_render_asset_loader_test PRIVATE gneiss::engine gneiss_tasks gneiss_yyjson gneiss_png granit::granit
)
target_link_libraries(gneiss_render_asset_loader_test PRIVATE granit::asset_tools)

target_compile_features(gneiss_render_asset_loader_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_asset_loader_test)
gneiss_target_output_directories(gneiss_render_asset_loader_test)
add_test(NAME gneiss.render_asset_loader COMMAND gneiss_render_asset_loader_test)

add_executable(
  gneiss_texture_load_service_test
  render/texture_load_service_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/detail/texture_load_service.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/detail/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/asset_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/png_decoder.cpp"
)
target_include_directories(gneiss_texture_load_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_texture_load_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(
  gneiss_texture_load_service_test PRIVATE gneiss::engine gneiss_tasks gneiss_yyjson gneiss_png granit::granit
)
target_compile_features(gneiss_texture_load_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_load_service_test)
gneiss_target_output_directories(gneiss_texture_load_service_test)
add_test(NAME gneiss.texture-load-service COMMAND gneiss_texture_load_service_test)

add_executable(gneiss_texture_load_application_test asset/texture_load_application_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp")
target_include_directories(gneiss_texture_load_application_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_texture_load_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_texture_load_application_test)
gneiss_target_output_directories(gneiss_texture_load_application_test)
add_test(NAME gneiss.texture-load-application COMMAND gneiss_texture_load_application_test)
set_tests_properties(gneiss.texture-load-service gneiss.texture-load-application PROPERTIES TIMEOUT 30)

add_executable(gneiss_asset_pixel_test render/asset_pixel_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp")
target_include_directories(gneiss_asset_pixel_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_asset_pixel_test PRIVATE gneiss::engine gneiss_tasks granit::asset_tools granit::granit)
gneiss_target_compile_warnings(gneiss_asset_pixel_test)
gneiss_target_output_directories(gneiss_asset_pixel_test)
add_test(NAME gneiss.asset-pixel COMMAND gneiss_asset_pixel_test)
add_test(NAME gneiss.pbr-negative-scale COMMAND gneiss_asset_pixel_test --probe-negative-scale)
set_tests_properties(gneiss.asset-pixel PROPERTIES TIMEOUT 60)
if(WIN32 AND GNEISS_TASK_THREADS)
  add_executable(gneiss_main_loop_response render/main_loop_response.cpp)
  target_include_directories(gneiss_main_loop_response PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_main_loop_response PRIVATE gneiss::engine gneiss_tasks user32)
  gneiss_target_compile_warnings(gneiss_main_loop_response)
  gneiss_target_output_directories(gneiss_main_loop_response)
  add_test(NAME gneiss.main-loop-response COMMAND gneiss_main_loop_response)
  set_tests_properties(gneiss.main-loop-response PROPERTIES TIMEOUT 30 LABELS "gpu")
endif()
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

add_executable(gneiss_scene_instance_test scene/scene_instance_test.cpp)
target_link_libraries(gneiss_scene_instance_test PRIVATE gneiss::engine gneiss_native_interface)
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
target_link_libraries(gneiss_scene_structural_reload_test PRIVATE gneiss::engine gneiss_tasks)
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
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/structural_diff.cpp"
)
target_include_directories(
  gneiss_structural_diff_test PRIVATE "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(gneiss_structural_diff_test PRIVATE gneiss_math_dependency)
target_compile_features(gneiss_structural_diff_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_structural_diff_test)
gneiss_target_output_directories(gneiss_structural_diff_test)
add_test(NAME gneiss.structural_diff COMMAND gneiss_structural_diff_test)

add_executable(
  gneiss_prefab_runtime_instance_test
  scene/prefab_runtime_instance_test.cpp
  "${PROJECT_SOURCE_DIR}/src/engine/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/png_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/detail/render_asset_loader.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/mesh/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/texture/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/asset_preparation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/prefab_property_data.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/prefab_property_override.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/prefab_runtime_instance.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/scene/scene_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/structural_diff.cpp"
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
add_executable(gneiss_scene_instantiation_test scene/scene_instantiation_test.cpp
  ${gneiss_scene_fixture_sources}
  "${PROJECT_SOURCE_DIR}/src/runtime/loading/scene_domain.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_instances.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_instance_edit.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_instance_prefab.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_instantiation.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/function/scene/scene_load_preparation.cpp")
# 测试直接构造私有 manager，编译其实现而不向 DLL 公共 ABI 导出内部类型。
target_sources(gneiss_scene_instantiation_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/asset_manager.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/input/action_map_codec.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/resource/input/action_map_validation.cpp")
target_include_directories(gneiss_scene_instantiation_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_scene_instantiation_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_scene_instantiation_test PRIVATE gneiss::engine gneiss_tasks gneiss_yyjson gneiss_png granit::granit)
target_compile_features(gneiss_scene_instantiation_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_instantiation_test)
gneiss_target_output_directories(gneiss_scene_instantiation_test)
add_test(NAME gneiss.scene-instantiation COMMAND gneiss_scene_instantiation_test)

add_executable(gneiss_scene_prefab_instance_test scene/scene_prefab_instance_test.cpp)
target_link_libraries(gneiss_scene_prefab_instance_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_definitions(
  gneiss_scene_prefab_instance_test
  PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets"
)
target_compile_features(gneiss_scene_prefab_instance_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_prefab_instance_test)
gneiss_target_output_directories(gneiss_scene_prefab_instance_test)
add_test(NAME gneiss.scene_prefab_instance COMMAND gneiss_scene_prefab_instance_test)

add_executable(gneiss_prefab_structural_reload_test scene/prefab_structural_reload_test.cpp)
target_link_libraries(gneiss_prefab_structural_reload_test PRIVATE gneiss::engine gneiss_tasks)
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




add_test(NAME gneiss.core-boundary
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_core_boundary.cmake")
add_test(NAME gneiss.core-boundary-negative
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/test_core_boundary.cmake")
add_test(NAME gneiss.public-api
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_public_api.cmake")

add_executable(gneiss_world_service_test world/world_service_test.cpp)
target_link_libraries(gneiss_world_service_test PRIVATE gneiss::engine)
target_include_directories(gneiss_world_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_world_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_world_service_test)
gneiss_target_output_directories(gneiss_world_service_test)
add_test(NAME gneiss.world-service-boundary COMMAND gneiss_world_service_test)

add_executable(gneiss_reflection_boundary_test meta/type_registry_boundary_test.cpp)
target_link_libraries(gneiss_reflection_boundary_test PRIVATE gneiss::engine)
target_include_directories(gneiss_reflection_boundary_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(gneiss_reflection_boundary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_reflection_boundary_test)
gneiss_target_output_directories(gneiss_reflection_boundary_test)
add_test(NAME gneiss.reflection-boundary COMMAND gneiss_reflection_boundary_test)

add_test(NAME gneiss.public-api-negative
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/test_public_api.cmake")

if(TARGET gneiss_dynamic_library AND (WIN32 OR UNIX) AND NOT EMSCRIPTEN)
  add_library(gneiss_dynamic_library_fixture SHARED platform/dynamic_library_fixture.c)
  set_target_properties(gneiss_dynamic_library_fixture PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  target_compile_features(gneiss_dynamic_library_fixture PRIVATE c_std_11)
  gneiss_target_compile_warnings(gneiss_dynamic_library_fixture)
  gneiss_target_output_directories(gneiss_dynamic_library_fixture)
  add_executable(gneiss_dynamic_library_test platform/dynamic_library_test.cpp)
  target_link_libraries(gneiss_dynamic_library_test PRIVATE gneiss_dynamic_library)
  gneiss_target_compile_warnings(gneiss_dynamic_library_test)
  gneiss_target_output_directories(gneiss_dynamic_library_test)
  add_dependencies(gneiss_dynamic_library_test gneiss_dynamic_library_fixture)
  add_test(NAME gneiss.platform.dynamic-library COMMAND gneiss_dynamic_library_test
    "$<TARGET_FILE:gneiss_dynamic_library_fixture>")
endif()

if(TARGET gneiss_editor_ui)
  add_subdirectory(editor)
endif()

# 共享 C++ 绘制描述的转换、借用数组及 C ABI 提交验证。
add_executable(gneiss_native_draw_submission_test native_draw_submission_test.cpp)
target_link_libraries(gneiss_native_draw_submission_test PRIVATE gneiss::engine gneiss_native_interface)
target_compile_features(gneiss_native_draw_submission_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_draw_submission_test)
gneiss_target_output_directories(gneiss_native_draw_submission_test)
add_test(NAME gneiss.native-draw-submission COMMAND gneiss_native_draw_submission_test)



add_executable(gneiss_task_header_visibility_test core/task_header_visibility_test.cpp)
target_link_libraries(gneiss_task_header_visibility_test PRIVATE gneiss_tasks)
gneiss_target_compile_warnings(gneiss_task_header_visibility_test)
gneiss_target_output_directories(gneiss_task_header_visibility_test)
add_test(NAME gneiss.task-header-visibility COMMAND gneiss_task_header_visibility_test)

add_test(NAME gneiss.cmake.dependency-source-cache
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_dependency_source_cache.cmake")
set_tests_properties(gneiss.cmake.dependency-source-cache PROPERTIES TIMEOUT 60)

add_executable(gneiss_native_result_test core/native_result_test.cpp)
target_include_directories(gneiss_native_result_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_native_result_test PRIVATE gneiss::engine)
target_compile_features(gneiss_native_result_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_result_test)
gneiss_target_output_directories(gneiss_native_result_test)
add_test(NAME gneiss.native_result COMMAND gneiss_native_result_test)

# 基础身份类型通过 Engine 链接，避免消费者自行编译出第二张字符串表。
add_executable(gneiss_core_identity_test core/core_identity_test.cpp)
target_link_libraries(gneiss_core_identity_test PRIVATE gneiss::engine gneiss_native_interface)
if(GNEISS_TASK_THREADS)
  target_link_libraries(gneiss_core_identity_test PRIVATE Threads::Threads)
  target_compile_definitions(gneiss_core_identity_test PRIVATE GNEISS_IDENTITY_TEST_THREADS)
endif()
gneiss_target_compile_warnings(gneiss_core_identity_test)
gneiss_target_output_directories(gneiss_core_identity_test)
add_test(NAME gneiss.core-identity COMMAND gneiss_core_identity_test)

# 直接编译用户工程模板，保证它只依赖最小游戏模块协议。

# 同一 Engine 中的类型名注册、并发首次注册及跨翻译单元身份。
add_executable(gneiss_type_id_test core/type_id_test.cpp core/type_id_other.cpp)
target_link_libraries(gneiss_type_id_test PRIVATE gneiss::engine gneiss_native_interface Threads::Threads)
gneiss_target_compile_warnings(gneiss_type_id_test)
gneiss_target_output_directories(gneiss_type_id_test)
add_test(NAME gneiss.type_id COMMAND gneiss_type_id_test)

add_executable(gneiss_type_registry_test meta/type_registry_test.cpp)
target_include_directories(gneiss_type_registry_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_type_registry_test PRIVATE gneiss::engine Threads::Threads)
target_compile_features(gneiss_type_registry_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_type_registry_test)
gneiss_target_output_directories(gneiss_type_registry_test)
add_test(NAME gneiss.type_registry COMMAND gneiss_type_registry_test)

# 数学类型、四元数分量顺序及 TRS 边界契约。
add_executable(gneiss_math_test core/math_test.cpp)
target_link_libraries(gneiss_math_test PRIVATE gneiss_value_interface)
target_compile_features(gneiss_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_math_test)
gneiss_target_output_directories(gneiss_math_test)
add_test(NAME gneiss.math COMMAND gneiss_math_test)

add_executable(gneiss_value_test core/value_test.cpp)
target_link_libraries(gneiss_value_test PRIVATE gneiss::engine gneiss_value_interface)
target_compile_features(gneiss_value_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_value_test)
gneiss_target_output_directories(gneiss_value_test)
add_test(NAME gneiss.value COMMAND gneiss_value_test)

add_executable(gneiss_type_builder_test meta/type_builder_test.cpp)
target_include_directories(gneiss_type_builder_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_type_builder_test PRIVATE gneiss::engine)
target_compile_features(gneiss_type_builder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_type_builder_test)
gneiss_target_output_directories(gneiss_type_builder_test)
add_test(NAME gneiss.type_builder COMMAND gneiss_type_builder_test)

add_executable(gneiss_serialization_test meta/serialization_test.cpp)
target_include_directories(gneiss_serialization_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_serialization_test PRIVATE gneiss::engine)
target_compile_features(gneiss_serialization_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_serialization_test)
gneiss_target_output_directories(gneiss_serialization_test)
add_test(NAME gneiss.serialization COMMAND gneiss_serialization_test)

# 仅编译接口，不链接后端，也不执行 GPU 测试。
add_library(gneiss_render_service_header_check OBJECT render/render_service_header_check.cpp)
target_include_directories(gneiss_render_service_header_check PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_render_service_header_check PRIVATE gneiss_value_interface)
target_compile_features(gneiss_render_service_header_check PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_service_header_check)

# 异步场景测试由宿主驱动任务，不使用生产同步加载入口。
foreach(test IN ITEMS gneiss_texture_load_application_test gneiss_asset_pixel_test gneiss_scene_load_baseline gneiss_texture_load_window_benchmark gneiss_scene_instance_test gneiss_scene_structural_reload_test gneiss_scene_instantiation_test gneiss_scene_prefab_instance_test gneiss_prefab_structural_reload_test)
  if(TARGET ${test})
    target_link_libraries(${test} PRIVATE gneiss_tasks)
    target_include_directories(${test} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  endif()
endforeach()

target_sources(gneiss_scene_instantiation_test PRIVATE
  "${PROJECT_SOURCE_DIR}/src/engine/function/resource/detail/texture_load_service.cpp")

# 异步资产测试由宿主推进解码任务。
foreach(test IN ITEMS gneiss_scene_description_test gneiss_prefab_description_test gneiss_prefab_runtime_instance_test)
  target_link_libraries(${test} PRIVATE gneiss_tasks)
endforeach()

foreach(test IN ITEMS gneiss_action_map_test gneiss_scene_description_test gneiss_prefab_description_test
    gneiss_scene_load_preparation_test gneiss_scene_instantiation_test gneiss_prefab_runtime_instance_test
    gneiss_texture_load_service_test gneiss_render_asset_loader_test)
  target_link_libraries(${test} PRIVATE)
endforeach()
