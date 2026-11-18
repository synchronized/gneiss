# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_native_render_lifecycle_test native_render_lifecycle_test.cpp)
target_link_libraries(gneiss_native_render_lifecycle_test PRIVATE gneiss::engine)
target_compile_features(gneiss_native_render_lifecycle_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_render_lifecycle_test)
gneiss_target_output_directories(gneiss_native_render_lifecycle_test)
set_target_properties(gneiss_native_render_lifecycle_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.native-render-lifecycle COMMAND gneiss_native_render_lifecycle_test)
set_tests_properties(gneiss.native-render-lifecycle PROPERTIES LABELS "cpu;integration")

add_executable(
  gneiss_render_resource_service_test
  render_resource_service_test.cpp
)
target_compile_definitions(gneiss_render_resource_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_render_resource_service_test PRIVATE gneiss::engine)
target_compile_features(gneiss_render_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_resource_service_test)
gneiss_target_output_directories(gneiss_render_resource_service_test)
set_target_properties(gneiss_render_resource_service_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.render_resource_service COMMAND gneiss_render_resource_service_test)
set_tests_properties(gneiss.render_resource_service PROPERTIES LABELS "cpu;integration")

add_executable(
  gneiss_pbr_material_resource_test
  pbr_material_resource_test.cpp
)
target_compile_definitions(gneiss_pbr_material_resource_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_pbr_material_resource_test PRIVATE gneiss::engine)
target_compile_features(gneiss_pbr_material_resource_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_pbr_material_resource_test)
gneiss_target_output_directories(gneiss_pbr_material_resource_test)
set_target_properties(gneiss_pbr_material_resource_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.pbr-material-resource COMMAND gneiss_pbr_material_resource_test)
set_tests_properties(gneiss.pbr-material-resource PROPERTIES LABELS "cpu;integration")

add_executable(
  gneiss_ui_draw_list_test
  ui_draw_list_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/ui_draw_list.cpp"
)
target_include_directories(gneiss_ui_draw_list_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_ui_draw_list_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_ui_draw_list_test PRIVATE gneiss::engine)
target_compile_features(gneiss_ui_draw_list_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_ui_draw_list_test)
gneiss_target_output_directories(gneiss_ui_draw_list_test)
set_target_properties(gneiss_ui_draw_list_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.ui_draw_list COMMAND gneiss_ui_draw_list_test)
set_tests_properties(gneiss.ui_draw_list PROPERTIES LABELS "cpu;unit")

add_executable(
  gneiss_debug_draw_list_test
  debug_draw_list_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/debug_draw_list.cpp"
)
target_compile_definitions(gneiss_debug_draw_list_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_debug_draw_list_test PRIVATE gneiss::engine)
target_compile_features(gneiss_debug_draw_list_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_debug_draw_list_test)
gneiss_target_output_directories(gneiss_debug_draw_list_test)
set_target_properties(gneiss_debug_draw_list_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.debug_draw_list COMMAND gneiss_debug_draw_list_test)
set_tests_properties(gneiss.debug_draw_list PROPERTIES LABELS "cpu;unit")

add_executable(
  gneiss_render_executor_test
  render_executor_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/executor.cpp"
)
target_include_directories(gneiss_render_executor_test PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_render_executor_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_render_executor_test PRIVATE gneiss::engine)
target_compile_features(gneiss_render_executor_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_executor_test)
gneiss_target_output_directories(gneiss_render_executor_test)
set_target_properties(gneiss_render_executor_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.render_executor COMMAND gneiss_render_executor_test)
set_tests_properties(gneiss.render_executor PROPERTIES LABELS "cpu;integration")
add_executable(
  gneiss_render_frame_packet_test
  render_frame_packet_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/ui_draw_list.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/scene.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/debug_draw_list.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/frame_packet.cpp"
)
target_include_directories(gneiss_render_frame_packet_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_render_frame_packet_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_render_frame_packet_test PRIVATE gneiss::engine)
target_compile_features(gneiss_render_frame_packet_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_frame_packet_test)
gneiss_target_output_directories(gneiss_render_frame_packet_test)
set_target_properties(gneiss_render_frame_packet_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.render_frame_packet COMMAND gneiss_render_frame_packet_test)
set_tests_properties(gneiss.render_frame_packet PROPERTIES LABELS "cpu;integration")
add_executable(
  gneiss_pbr_shader_resolver_test
  pbr_shader_resolver_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/backend/granit/pbr_shader_resolver.cpp"
)
target_include_directories(
  gneiss_pbr_shader_resolver_test
  PRIVATE "${PROJECT_SOURCE_DIR}/engine/src" "${PROJECT_BINARY_DIR}/engine/generated/pbr"
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
set_target_properties(gneiss_pbr_shader_resolver_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.pbr_shader_resolver COMMAND gneiss_pbr_shader_resolver_test)
set_tests_properties(gneiss.pbr_shader_resolver PROPERTIES LABELS "cpu;integration")

add_executable(
  gneiss_camera_math_test
  camera_math_test.cpp
)
target_link_libraries(gneiss_camera_math_test PRIVATE gneiss::engine)
target_compile_features(gneiss_camera_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_camera_math_test)
gneiss_target_output_directories(gneiss_camera_math_test)
set_target_properties(gneiss_camera_math_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.camera_math COMMAND gneiss_camera_math_test)
set_tests_properties(gneiss.camera_math PROPERTIES LABELS "cpu;unit")

add_executable(
  gneiss_normal_math_test
  normal_math_test.cpp
)
target_link_libraries(gneiss_normal_math_test PRIVATE gneiss_math)
target_compile_features(gneiss_normal_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_normal_math_test)
gneiss_target_output_directories(gneiss_normal_math_test)
set_target_properties(gneiss_normal_math_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.normal_math COMMAND gneiss_normal_math_test)
set_tests_properties(gneiss.normal_math PROPERTIES LABELS "cpu;unit")

add_executable(
  gneiss_scene_projection_math_test
  scene_projection_math_test.cpp
)
target_link_libraries(gneiss_scene_projection_math_test PRIVATE gneiss_math gneiss_base)
target_compile_features(gneiss_scene_projection_math_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_projection_math_test)
gneiss_target_output_directories(gneiss_scene_projection_math_test)
set_target_properties(gneiss_scene_projection_math_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.scene_projection_math COMMAND gneiss_scene_projection_math_test)
set_tests_properties(gneiss.scene_projection_math PROPERTIES LABELS "cpu;unit")

# 私有解码/缓存符号保留本地编译；已导出的格式与摘要实现由 Engine 提供。
add_executable(
  gneiss_render_asset_loader_test
  render_asset_loader_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_decode.cpp"
)
target_include_directories(gneiss_render_asset_loader_test PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_render_asset_loader_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(
  gneiss_render_asset_loader_test PRIVATE gneiss::engine gneiss_tasks gneiss_yyjson gneiss_png granit::granit
)
target_link_libraries(gneiss_render_asset_loader_test PRIVATE granit::asset_tools)

target_compile_features(gneiss_render_asset_loader_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_asset_loader_test)
gneiss_target_output_directories(gneiss_render_asset_loader_test)
set_target_properties(gneiss_render_asset_loader_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.render_asset_loader COMMAND gneiss_render_asset_loader_test)
set_tests_properties(gneiss.render_asset_loader PROPERTIES LABELS "cpu;integration")

# 私有解码/缓存符号保留本地编译；已导出的格式与摘要实现由 Engine 提供。
add_executable(
  gneiss_texture_load_service_test
  texture_load_service_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/resource_cache.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_decode.cpp"
)
target_include_directories(gneiss_texture_load_service_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_texture_load_service_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(
  gneiss_texture_load_service_test PRIVATE gneiss::engine gneiss_tasks gneiss_yyjson gneiss_png granit::granit
)
target_compile_features(gneiss_texture_load_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_load_service_test)
gneiss_target_output_directories(gneiss_texture_load_service_test)
set_target_properties(gneiss_texture_load_service_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.texture-load-service COMMAND gneiss_texture_load_service_test)
set_tests_properties(gneiss.texture-load-service PROPERTIES LABELS "cpu;integration")

add_executable(gneiss_asset_pixel_test asset_pixel_test.cpp)
target_include_directories(gneiss_asset_pixel_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_asset_pixel_test PRIVATE gneiss::engine gneiss_tasks granit::asset_tools granit::granit)
gneiss_target_compile_warnings(gneiss_asset_pixel_test)
gneiss_target_output_directories(gneiss_asset_pixel_test)
set_target_properties(gneiss_asset_pixel_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.asset-pixel COMMAND gneiss_asset_pixel_test)
add_test(NAME gneiss.pbr-negative-scale COMMAND gneiss_asset_pixel_test --probe-negative-scale)
set_tests_properties(gneiss.asset-pixel gneiss.pbr-negative-scale PROPERTIES TIMEOUT 60 LABELS "gpu;integration")
if(WIN32 AND GNEISS_TASK_THREADS)
  add_executable(gneiss_main_loop_response main_loop_response.cpp)
    target_link_libraries(gneiss_main_loop_response PRIVATE gneiss::engine gneiss_tasks user32)
  gneiss_target_compile_warnings(gneiss_main_loop_response)
  gneiss_target_output_directories(gneiss_main_loop_response)
  set_target_properties(gneiss_main_loop_response PROPERTIES FOLDER "Tests")
  add_test(NAME gneiss.main-loop-response COMMAND gneiss_main_loop_response)
  set_tests_properties(gneiss.main-loop-response PROPERTIES TIMEOUT 30 LABELS "gpu")
endif()
add_executable(gneiss_scene_load_baseline scene_load_baseline.cpp)
target_include_directories(gneiss_scene_load_baseline PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_scene_load_baseline PRIVATE gneiss::engine gneiss_tasks)
if(WIN32)
  target_link_libraries(gneiss_scene_load_baseline PRIVATE psapi)
endif()
gneiss_target_compile_warnings(gneiss_scene_load_baseline)
gneiss_target_output_directories(gneiss_scene_load_baseline)
set_target_properties(gneiss_scene_load_baseline PROPERTIES FOLDER "Tests")

add_executable(gneiss_texture_load_window_benchmark texture_load_window_benchmark.cpp)
target_link_libraries(gneiss_texture_load_window_benchmark PRIVATE gneiss::engine gneiss_tasks)
if(WIN32)
  target_link_libraries(gneiss_texture_load_window_benchmark PRIVATE user32)
endif()
gneiss_target_compile_warnings(gneiss_texture_load_window_benchmark)
gneiss_target_output_directories(gneiss_texture_load_window_benchmark)
set_target_properties(gneiss_texture_load_window_benchmark PROPERTIES FOLDER "Tests")

# 共享 C++ 绘制描述的转换、借用数组及 C ABI 提交验证。
add_executable(gneiss_native_draw_submission_test native_draw_submission_test.cpp)
target_link_libraries(gneiss_native_draw_submission_test PRIVATE gneiss::engine)
target_compile_features(gneiss_native_draw_submission_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_native_draw_submission_test)
gneiss_target_output_directories(gneiss_native_draw_submission_test)
set_target_properties(gneiss_native_draw_submission_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.native-draw-submission COMMAND gneiss_native_draw_submission_test)
set_tests_properties(gneiss.native-draw-submission PROPERTIES LABELS "cpu;integration")

# 仅编译接口，不链接后端，也不执行 GPU 测试。
add_library(gneiss_render_service_header_check OBJECT render_service_header_check.cpp)
target_link_libraries(gneiss_render_service_header_check PRIVATE gneiss_math gneiss_base)
target_compile_features(gneiss_render_service_header_check PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_service_header_check)
set_target_properties(gneiss_render_service_header_check PROPERTIES FOLDER "Tests")

target_link_libraries(gneiss_render_executor_test PRIVATE gneiss_base)

if(NOT EMSCRIPTEN AND GNEISS_TASK_THREADS)
  foreach(mode render-frame render-command)
    add_test(NAME gneiss.fatal-${mode} COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:gneiss_render_executor_test>" "-DMODE=${mode}"
      -P "${PROJECT_SOURCE_DIR}/tests/engine/core/check_fatal.cmake")
    set_tests_properties(gneiss.fatal-${mode} PROPERTIES LABELS "cpu;integration;death")
    set_tests_properties(gneiss.fatal-${mode} PROPERTIES TIMEOUT 15)
  endforeach()
endif()

add_executable(gneiss_render_scene_test
  render_scene_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/executor.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/scene.cpp")
target_include_directories(gneiss_render_scene_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_render_scene_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_render_scene_test PRIVATE gneiss::engine gneiss_base)
gneiss_target_compile_warnings(gneiss_render_scene_test)
gneiss_target_output_directories(gneiss_render_scene_test)
set_target_properties(gneiss_render_scene_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.render_scene COMMAND gneiss_render_scene_test)
set_tests_properties(gneiss.render_scene PROPERTIES LABELS "cpu;unit")

set_tests_properties(gneiss.texture-load-service PROPERTIES TIMEOUT 30)
