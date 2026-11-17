# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_json_format_test json_format_test.cpp)
target_link_libraries(gneiss_json_format_test PRIVATE gneiss_yyjson)
target_compile_features(gneiss_json_format_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_json_format_test)
gneiss_target_output_directories(gneiss_json_format_test)
set_target_properties(gneiss_json_format_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.json_format COMMAND gneiss_json_format_test)

add_executable(gneiss_scene_description_test scene_description_test.cpp)
# 通过正式序列化与资产加载入口验证，不重复编译 Engine 源码。
target_link_libraries(gneiss_scene_description_test PRIVATE gneiss::engine gneiss_tasks)
target_compile_features(gneiss_scene_description_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_scene_description_test)
gneiss_target_output_directories(gneiss_scene_description_test)
set_target_properties(gneiss_scene_description_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.scene_description COMMAND gneiss_scene_description_test)

add_executable(gneiss_prefab_description_test prefab_description_test.cpp)
# 通过正式序列化与资产加载入口验证，不重复编译 Engine 源码。
target_link_libraries(gneiss_prefab_description_test PRIVATE gneiss::engine gneiss_tasks)
target_compile_features(gneiss_prefab_description_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_description_test)
gneiss_target_output_directories(gneiss_prefab_description_test)
set_target_properties(gneiss_prefab_description_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.prefab_description COMMAND gneiss_prefab_description_test)

# 覆盖准备/校验未导出；当前与编辑器静态库混链会产生导出定义冲突，保留白盒组合待专项收敛。
add_executable(
  gneiss_prefab_property_override_test
  prefab_property_override_test.cpp
  "${PROJECT_SOURCE_DIR}/editor/documents/prefab_edit.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/resource/asset_uri.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/resource/scene/prefab_property_data.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/scene/prefab_property_override.cpp"
)
target_include_directories(gneiss_prefab_property_override_test PRIVATE "${PROJECT_SOURCE_DIR}/editor" "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(gneiss_prefab_property_override_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_prefab_property_override_test PRIVATE gneiss::engine gneiss_yyjson EnTT::EnTT)
target_compile_features(gneiss_prefab_property_override_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_prefab_property_override_test)
gneiss_target_output_directories(gneiss_prefab_property_override_test)
set_target_properties(gneiss_prefab_property_override_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.prefab_property_override COMMAND gneiss_prefab_property_override_test)

set_tests_properties(gneiss.json_format gneiss.prefab_property_override PROPERTIES LABELS "cpu;unit")
set_tests_properties(gneiss.scene_description gneiss.prefab_description PROPERTIES LABELS "cpu;integration")
