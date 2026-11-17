# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(
  gneiss_input_service_test
  input_service_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/resource/input/action_map_validation.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/function/input/input_service.cpp"
)
target_include_directories(gneiss_input_service_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_input_service_test PRIVATE gneiss::engine)
target_compile_features(gneiss_input_service_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_input_service_test)
gneiss_target_output_directories(gneiss_input_service_test)
set_target_properties(gneiss_input_service_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.input_service COMMAND gneiss_input_service_test)

add_executable(
  gneiss_action_map_test
  action_map_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/resource/input/action_map_validation.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/resource/asset_uri.cpp"
)
target_include_directories(gneiss_action_map_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(gneiss_action_map_test PRIVATE GNEISS_BUILDING_LIBRARY)
target_link_libraries(gneiss_action_map_test PRIVATE gneiss::engine gneiss_yyjson gneiss_tasks)
target_compile_features(gneiss_action_map_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_action_map_test)
gneiss_target_output_directories(gneiss_action_map_test)
set_target_properties(gneiss_action_map_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.action_map COMMAND gneiss_action_map_test)

add_executable(gneiss_cpp_input_test cpp_input_test.cpp)
target_link_libraries(gneiss_cpp_input_test PRIVATE gneiss::engine gneiss_native_interface gneiss_tasks)
target_compile_definitions(gneiss_cpp_input_test
  PRIVATE GNEISS_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/tests/data/assets")
target_compile_features(gneiss_cpp_input_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_cpp_input_test)
gneiss_target_output_directories(gneiss_cpp_input_test)
set_target_properties(gneiss_cpp_input_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.cpp_input COMMAND gneiss_cpp_input_test)
