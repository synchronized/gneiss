# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_asset_import_sdk STATIC
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_index.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_index.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_import_sdk.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_import_sdk.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_writer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_writer.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/import_ir.h"
)
target_include_directories(
  gneiss_asset_import_sdk PUBLIC "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(gneiss_asset_import_sdk PRIVATE fastgltf::fastgltf gneiss_yyjson)
target_compile_features(gneiss_asset_import_sdk PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_asset_import_sdk)

add_executable(gneiss_assetc main.cpp)
target_include_directories(gneiss_assetc PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_assetc PRIVATE gneiss_asset_import_sdk)
target_compile_features(gneiss_assetc PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_assetc)
gneiss_target_output_directories(gneiss_assetc)

if(GNEISS_BUILD_TESTING AND BUILD_TESTING)
  add_executable(
    gneiss_gltf_importer_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/gltf_importer_test.cpp"
  )
  target_include_directories(gneiss_gltf_importer_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_gltf_importer_test PRIVATE gneiss_asset_import_sdk)
  target_compile_definitions(
    gneiss_gltf_importer_test
    PRIVATE GNEISS_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
  )
  target_compile_features(gneiss_gltf_importer_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_gltf_importer_test)
  gneiss_target_output_directories(gneiss_gltf_importer_test)
  add_test(NAME gneiss.tooling.gltf_importer COMMAND gneiss_gltf_importer_test)

  add_executable(
    gneiss_asset_writer_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/asset_writer_test.cpp"
  )
  target_include_directories(gneiss_asset_writer_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(
    gneiss_asset_writer_test PRIVATE gneiss_asset_import_sdk gneiss::engine
  )
  target_compile_definitions(
    gneiss_asset_writer_test
    PRIVATE GNEISS_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
  )
  target_compile_features(gneiss_asset_writer_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_asset_writer_test)
  gneiss_target_output_directories(gneiss_asset_writer_test)
  add_test(NAME gneiss.tooling.asset_writer COMMAND gneiss_asset_writer_test)

  add_executable(
    gneiss_asset_import_sdk_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/asset_import_sdk_test.cpp"
  )
  target_include_directories(gneiss_asset_import_sdk_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_asset_import_sdk_test PRIVATE gneiss_asset_import_sdk)
  target_compile_definitions(
    gneiss_asset_import_sdk_test
    PRIVATE GNEISS_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
  )
  target_compile_features(gneiss_asset_import_sdk_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_asset_import_sdk_test)
  gneiss_target_output_directories(gneiss_asset_import_sdk_test)
  add_test(NAME gneiss.tooling.asset_import_sdk COMMAND gneiss_asset_import_sdk_test)

  add_executable(
    gneiss_asset_index_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/asset_index_test.cpp"
  )
  target_include_directories(gneiss_asset_index_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_asset_index_test PRIVATE gneiss_asset_import_sdk)
  target_compile_features(gneiss_asset_index_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_asset_index_test)
  gneiss_target_output_directories(gneiss_asset_index_test)
  add_test(NAME gneiss.tooling.asset_index COMMAND gneiss_asset_index_test)

  add_executable(
    gneiss_gltf_container_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/gltf_container_test.cpp"
  )
  target_include_directories(gneiss_gltf_container_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_gltf_container_test PRIVATE gneiss_asset_import_sdk)
  target_compile_definitions(
    gneiss_gltf_container_test
    PRIVATE GNEISS_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
  )
  target_compile_features(gneiss_gltf_container_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_gltf_container_test)
  gneiss_target_output_directories(gneiss_gltf_container_test)
  add_test(NAME gneiss.tooling.gltf_container COMMAND gneiss_gltf_container_test)

  add_test(
    NAME gneiss.tooling.assetc_inspect
    COMMAND gneiss_assetc inspect "${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf"
  )
  set_tests_properties(
    gneiss.tooling.assetc_inspect
    PROPERTIES PASS_REGULAR_EXPRESSION "场景=1 节点=1 网格=1 图元=1 材质=1 图像=1"
  )
  add_test(
    NAME gneiss.tooling.lantern_import
    COMMAND
      "${CMAKE_COMMAND}"
      "-DGNEISS_ASSETC=$<TARGET_FILE:gneiss_assetc>"
      "-DGNEISS_LANTERN_SOURCE=${PROJECT_SOURCE_DIR}/examples/lantern_gallery/assets/source/Lantern.glb"
      "-DGNEISS_LANTERN_OUTPUT=${PROJECT_BINARY_DIR}/tests/lantern-import"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_lantern_import.cmake"
  )
endif()
