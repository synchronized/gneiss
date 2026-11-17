# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

find_package(Threads REQUIRED)

add_subdirectory(core)
add_subdirectory(meta)
add_subdirectory(serialization)
add_subdirectory(asset)
add_subdirectory(scene)
add_subdirectory(render)
add_subdirectory(platform)
add_subdirectory(input)
add_subdirectory(log)
add_subdirectory(headers)
add_subdirectory(application)

# 跨领域装配与构建规则验收。

add_test(
  NAME gneiss.granit_version_cache
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/testing/check_granit_version_cache.cmake"
)

add_test(NAME gneiss.core-boundary
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/testing/check_core_boundary.cmake")
add_test(NAME gneiss.core-boundary-negative
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/testing/test_core_boundary.cmake")
add_test(NAME gneiss.public-api
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/testing/check_public_api.cmake")

add_test(NAME gneiss.public-api-negative
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/testing/test_public_api.cmake")

add_test(NAME gneiss.cmake.dependency-source-cache
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DGNEISS_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/testing/check_dependency_source_cache.cmake")
set_tests_properties(gneiss.cmake.dependency-source-cache PROPERTIES TIMEOUT 60)

# 异步场景测试由宿主驱动任务，不使用生产同步加载入口。
foreach(test IN ITEMS gneiss_texture_load_application_test gneiss_asset_pixel_test gneiss_scene_load_baseline gneiss_texture_load_window_benchmark gneiss_scene_instance_test gneiss_scene_instantiation_test gneiss_scene_prefab_instance_test)
  if(TARGET ${test})
    target_link_libraries(${test} PRIVATE gneiss_tasks)
      endif()
endforeach()
