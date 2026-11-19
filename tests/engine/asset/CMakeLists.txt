# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

if(GNEISS_ENABLE_PROFILING AND WIN32)
  # 手动诊断夹具只链接 Tracy，不携带引擎、调度器或渲染器。
  add_executable(gneiss_filesystem_trace_probe filesystem_trace_probe.cpp)
    target_compile_features(gneiss_filesystem_trace_probe PRIVATE cxx_std_20)
  gneiss_target_profiling(gneiss_filesystem_trace_probe)
  set_target_properties(gneiss_filesystem_trace_probe PROPERTIES FOLDER "Tests")
  gneiss_target_compile_warnings(gneiss_filesystem_trace_probe)
  gneiss_target_output_directories(gneiss_filesystem_trace_probe)
endif()

add_executable(
  gneiss_png_decoder_test
  png_decoder_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/png_decoder.cpp"
)
# 分配失败注入依赖可执行文件内的 operator new，保留本地编译的解码实现。
target_compile_definitions(gneiss_png_decoder_test PRIVATE GNEISS_BUILDING_ENGINE GNEISS_RESOURCE_STATIC_DEFINE)
target_link_libraries(gneiss_png_decoder_test PRIVATE gneiss::engine gneiss_png)
target_compile_features(gneiss_png_decoder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_png_decoder_test)
gneiss_target_output_directories(gneiss_png_decoder_test)
set_target_properties(gneiss_png_decoder_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.png_decoder COMMAND gneiss_png_decoder_test)
set_tests_properties(gneiss.png_decoder PROPERTIES LABELS "cpu;integration")

add_executable(gneiss_asset_uri_test asset_uri_test.cpp)
target_link_libraries(gneiss_asset_uri_test PRIVATE gneiss::engine)
target_compile_features(gneiss_asset_uri_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_asset_uri_test)
gneiss_target_output_directories(gneiss_asset_uri_test)
set_target_properties(gneiss_asset_uri_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.asset_uri COMMAND gneiss_asset_uri_test)
set_tests_properties(gneiss.asset_uri PROPERTIES LABELS "cpu;unit")

# 私有解码/缓存符号保留本地编译；已导出的格式与摘要实现由 Engine 提供。
add_executable(
  gneiss_resource_service_test
  resource_service_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/resource_cache.cpp"
)
target_include_directories(gneiss_resource_service_test PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_resource_service_test PRIVATE GNEISS_BUILDING_ENGINE GNEISS_RESOURCE_STATIC_DEFINE)
target_link_libraries(gneiss_resource_service_test PRIVATE gneiss::engine)
target_compile_features(gneiss_resource_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_resource_service_test)
gneiss_target_output_directories(gneiss_resource_service_test)
set_target_properties(gneiss_resource_service_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.asset_manager COMMAND gneiss_resource_service_test)
set_tests_properties(gneiss.asset_manager PROPERTIES LABELS "cpu;integration")

add_executable(
  gneiss_mesh_binary_test
  mesh_binary_test.cpp
)
target_include_directories(gneiss_mesh_binary_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_mesh_binary_test PRIVATE gneiss_resource)
target_link_libraries(gneiss_mesh_binary_test PRIVATE gneiss_math_dependency)
target_compile_features(gneiss_mesh_binary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_mesh_binary_test)
gneiss_target_output_directories(gneiss_mesh_binary_test)
set_target_properties(gneiss_mesh_binary_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.mesh_binary COMMAND gneiss_mesh_binary_test)
set_tests_properties(gneiss.mesh_binary PROPERTIES LABELS "cpu;unit")

add_executable(
  gneiss_texture_ktx2_test
  texture_ktx2_test.cpp
)
target_include_directories(gneiss_texture_ktx2_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_texture_ktx2_test PRIVATE gneiss_resource)
target_compile_features(gneiss_texture_ktx2_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_ktx2_test)
gneiss_target_output_directories(gneiss_texture_ktx2_test)
set_target_properties(gneiss_texture_ktx2_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.texture_ktx2 COMMAND gneiss_texture_ktx2_test)
set_tests_properties(gneiss.texture_ktx2 PROPERTIES LABELS "cpu;unit")

add_executable(
  gneiss_texture_binary_test
  texture_binary_test.cpp
)
target_include_directories(gneiss_texture_binary_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_texture_binary_test PRIVATE gneiss_resource)
target_compile_features(gneiss_texture_binary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_texture_binary_test)
gneiss_target_output_directories(gneiss_texture_binary_test)
set_target_properties(gneiss_texture_binary_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.texture_binary COMMAND gneiss_texture_binary_test)
set_tests_properties(gneiss.texture_binary PROPERTIES LABELS "cpu;unit")

# CPU 准备复用 Engine 的反射描述 codec，不创建 Application 或 GPU。
add_executable(gneiss_render_asset_preparation_test
  asset_preparation_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/mesh_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/texture_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_decode.cpp")
target_compile_definitions(gneiss_render_asset_preparation_test PRIVATE GNEISS_BUILDING_ENGINE GNEISS_RESOURCE_STATIC_DEFINE)
target_include_directories(gneiss_render_asset_preparation_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_render_asset_preparation_test PRIVATE
  gneiss::engine gneiss_yyjson granit::granit Threads::Threads)
target_compile_features(gneiss_render_asset_preparation_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_render_asset_preparation_test)
gneiss_target_output_directories(gneiss_render_asset_preparation_test)
set_target_properties(gneiss_render_asset_preparation_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.render_asset_preparation COMMAND gneiss_render_asset_preparation_test)
set_tests_properties(gneiss.render_asset_preparation PROPERTIES LABELS "cpu;integration")

add_executable(gneiss_texture_load_application_test texture_load_application_test.cpp)
target_link_libraries(gneiss_texture_load_application_test PRIVATE gneiss::engine gneiss_tasks)
gneiss_target_compile_warnings(gneiss_texture_load_application_test)
gneiss_target_output_directories(gneiss_texture_load_application_test)
set_target_properties(gneiss_texture_load_application_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.texture-load-application COMMAND gneiss_texture_load_application_test)
set_tests_properties(gneiss.texture-load-application PROPERTIES LABELS "cpu;integration")

target_link_libraries(gneiss_png_decoder_test PRIVATE gneiss_base)

set_tests_properties(gneiss.texture-load-application PROPERTIES TIMEOUT 30)
