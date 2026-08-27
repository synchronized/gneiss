# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(gneiss_consumer LANGUAGES C CXX)

include(CTest)
find_package(gneiss CONFIG REQUIRED)

add_executable(gneiss_c_consumer c_consumer.c)
target_link_libraries(gneiss_c_consumer PRIVATE gneiss::gneiss)
set_target_properties(
  gneiss_c_consumer
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)

add_executable(gneiss_cpp_consumer cpp_consumer.cpp)
target_link_libraries(gneiss_cpp_consumer PRIVATE gneiss::gneiss)
target_compile_features(gneiss_cpp_consumer PRIVATE cxx_std_20)
target_compile_definitions(
  gneiss_cpp_consumer
  PRIVATE GNEISS_CONSUMER_ASSET_ROOT="${CMAKE_CURRENT_LIST_DIR}/../data/assets"
)

add_test(NAME gneiss.consumer.c COMMAND gneiss_c_consumer)
add_test(NAME gneiss.consumer.cpp COMMAND gneiss_cpp_consumer)
set_tests_properties(
  gneiss.consumer.c gneiss.consumer.cpp
  PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${GNEISS_RUNTIME_DIR}"
)
