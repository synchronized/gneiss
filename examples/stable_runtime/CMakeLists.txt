# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(gneiss_stable_runtime_consumer LANGUAGES CXX)

include(CTest)
find_package(gneiss CONFIG REQUIRED)

set(stable_runtime_asset_root "${CMAKE_CURRENT_BINARY_DIR}/assets")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../temple/assets/" DESTINATION "${stable_runtime_asset_root}")

add_executable(gneiss_stable_runtime_consumer main.cpp)
target_link_libraries(gneiss_stable_runtime_consumer PRIVATE gneiss::gneiss)
target_compile_features(gneiss_stable_runtime_consumer PRIVATE cxx_std_20)
target_compile_definitions(
  gneiss_stable_runtime_consumer
  PRIVATE GNEISS_STABLE_RUNTIME_ASSET_ROOT="${stable_runtime_asset_root}"
)

if(BUILD_TESTING)
  add_test(
    NAME gneiss.stable_runtime_consumer
    COMMAND gneiss_stable_runtime_consumer --smoke
  )
  set(runtime_environment "PATH=path_list_prepend:${GNEISS_RUNTIME_DIR}")
  foreach(runtime_dir IN LISTS GNEISS_CONSUMER_RUNTIME_DIRS)
    list(APPEND runtime_environment "PATH=path_list_prepend:${runtime_dir}")
  endforeach()
  set_tests_properties(
    gneiss.stable_runtime_consumer
    PROPERTIES ENVIRONMENT_MODIFICATION "${runtime_environment}" TIMEOUT 30
  )
endif()
