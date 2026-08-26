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

add_library(gneiss_c_header_test OBJECT headers/gneiss_h.c headers/result_h.c headers/rid_h.c)
target_link_libraries(gneiss_c_header_test PRIVATE gneiss::gneiss)
target_compile_features(gneiss_c_header_test PRIVATE c_std_11)
gneiss_target_compile_warnings(gneiss_c_header_test)

add_library(
  gneiss_cpp_header_test OBJECT headers/gneiss_hpp.cpp headers/result_hpp.cpp headers/rid_hpp.cpp
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

if(TARGET gneiss_version_example)
  add_test(NAME gneiss.version_example COMMAND gneiss_version_example)
  set_tests_properties(
    gneiss.version_example
    PROPERTIES PASS_REGULAR_EXPRESSION "gneiss ${PROJECT_VERSION_MAJOR}\\.${PROJECT_VERSION_MINOR}\\.${PROJECT_VERSION_PATCH}"
  )
endif()
