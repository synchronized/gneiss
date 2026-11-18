# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_property_access_test property_access_test.cpp)
target_link_libraries(gneiss_property_access_test PRIVATE gneiss::engine)
target_compile_features(gneiss_property_access_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_property_access_test)
gneiss_target_output_directories(gneiss_property_access_test)
set_target_properties(gneiss_property_access_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.property_access COMMAND gneiss_property_access_test)
if(NOT EMSCRIPTEN)
  add_test(NAME gneiss.meta-access-contract COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:gneiss_property_access_test>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_access_contract.cmake")
  set_tests_properties(gneiss.meta-access-contract PROPERTIES LABELS "cpu;death" TIMEOUT 30)
endif()

add_executable(gneiss_reflection_boundary_test type_registry_boundary_test.cpp)
target_link_libraries(gneiss_reflection_boundary_test PRIVATE gneiss::engine)
target_compile_features(gneiss_reflection_boundary_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_reflection_boundary_test)
gneiss_target_output_directories(gneiss_reflection_boundary_test)
set_target_properties(gneiss_reflection_boundary_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.reflection-boundary COMMAND gneiss_reflection_boundary_test)

add_executable(gneiss_type_registry_test type_registry_test.cpp)
target_link_libraries(gneiss_type_registry_test PRIVATE gneiss::engine Threads::Threads)
target_compile_features(gneiss_type_registry_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_type_registry_test)
gneiss_target_output_directories(gneiss_type_registry_test)
set_target_properties(gneiss_type_registry_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.type_registry COMMAND gneiss_type_registry_test)

add_executable(gneiss_type_builder_test type_builder_test.cpp)
target_link_libraries(gneiss_type_builder_test PRIVATE gneiss::engine)
target_compile_features(gneiss_type_builder_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_type_builder_test)
gneiss_target_output_directories(gneiss_type_builder_test)
set_target_properties(gneiss_type_builder_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.type_builder COMMAND gneiss_type_builder_test)

add_executable(gneiss_serialization_test serialization_test.cpp)
target_link_libraries(gneiss_serialization_test PRIVATE gneiss::engine)
target_compile_features(gneiss_serialization_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_serialization_test)
gneiss_target_output_directories(gneiss_serialization_test)
set_target_properties(gneiss_serialization_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.serialization COMMAND gneiss_serialization_test)

add_executable(gneiss_serialization_archive_test archive_test.cpp)
target_link_libraries(gneiss_serialization_archive_test PRIVATE gneiss::engine)
target_compile_features(gneiss_serialization_archive_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_serialization_archive_test)
gneiss_target_output_directories(gneiss_serialization_archive_test)
set_target_properties(gneiss_serialization_archive_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.serialization-archive COMMAND gneiss_serialization_archive_test)

add_executable(gneiss_reflection_json_test reflection_json_test.cpp nested_json_test.cpp)
# JSON 分配失败注入需要私有适配器；不重复编译其实现，也不扩大生产导出。
target_include_directories(gneiss_reflection_json_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_reflection_json_test PRIVATE gneiss::engine gneiss_yyjson)
target_compile_features(gneiss_reflection_json_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_reflection_json_test)
gneiss_target_output_directories(gneiss_reflection_json_test)
set_target_properties(gneiss_reflection_json_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.reflection-json COMMAND gneiss_reflection_json_test)

add_executable(gneiss_meta_instance_test instance_test.cpp)
target_link_libraries(gneiss_meta_instance_test PRIVATE gneiss::engine)
target_compile_features(gneiss_meta_instance_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_meta_instance_test)
gneiss_target_output_directories(gneiss_meta_instance_test)
set_target_properties(gneiss_meta_instance_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.meta-instance COMMAND gneiss_meta_instance_test)

add_executable(gneiss_meta_container_test container_test.cpp)
target_link_libraries(gneiss_meta_container_test PRIVATE gneiss::engine)
target_compile_features(gneiss_meta_container_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_meta_container_test)
gneiss_target_output_directories(gneiss_meta_container_test)
set_target_properties(gneiss_meta_container_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.meta-container COMMAND gneiss_meta_container_test)

add_executable(gneiss_meta_type_ops_test type_ops_test.cpp)
target_link_libraries(gneiss_meta_type_ops_test PRIVATE gneiss::engine)
target_compile_features(gneiss_meta_type_ops_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_meta_type_ops_test)
gneiss_target_output_directories(gneiss_meta_type_ops_test)
set_target_properties(gneiss_meta_type_ops_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.meta-type-ops COMMAND gneiss_meta_type_ops_test)

add_executable(gneiss_default_registry_test default_registry_test.cpp)
target_link_libraries(gneiss_default_registry_test PRIVATE gneiss::engine)
target_compile_features(gneiss_default_registry_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_default_registry_test)
gneiss_target_output_directories(gneiss_default_registry_test)
set_target_properties(gneiss_default_registry_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.meta-default-registry COMMAND gneiss_default_registry_test)
add_test(NAME gneiss.meta-default-registry-failure COMMAND gneiss_default_registry_test failure)
target_link_libraries(gneiss_default_registry_test PRIVATE gneiss_core)
add_test(NAME gneiss.meta-default-registry-exception COMMAND "${CMAKE_COMMAND}"
  "-DPROGRAM=$<TARGET_FILE:gneiss_default_registry_test>" -DMODE=throw
  -P "${PROJECT_SOURCE_DIR}/tests/engine/core/check_fatal.cmake")
set_tests_properties(gneiss.meta-default-registry-exception PROPERTIES TIMEOUT 15)
if(NOT EMSCRIPTEN AND GNEISS_TASK_THREADS)
  target_compile_definitions(gneiss_default_registry_test PRIVATE GNEISS_TEST_THREADS=1)
  target_link_libraries(gneiss_default_registry_test PRIVATE Threads::Threads)
endif()

# 经调用路径核查，以下用例只操作内存中的反射/序列化对象，不初始化窗口或渲染器。
set_tests_properties(
  gneiss.property_access gneiss.reflection-boundary gneiss.type_registry gneiss.type_builder
  gneiss.serialization gneiss.serialization-archive gneiss.reflection-json
  gneiss.meta-instance gneiss.meta-container gneiss.meta-type-ops
  PROPERTIES LABELS "cpu;unit")
# 默认注册表覆盖内建资源注册与序列化装配；异常用例由子进程验证终止诊断。
set_tests_properties(gneiss.meta-default-registry gneiss.meta-default-registry-failure
  PROPERTIES LABELS "cpu;integration")
set_tests_properties(gneiss.meta-default-registry-exception
  PROPERTIES LABELS "cpu;integration;death")
