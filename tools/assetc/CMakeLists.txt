# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_asset_pipeline_internal STATIC
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/import_ir.h"
)
target_include_directories(
  gneiss_asset_pipeline_internal PRIVATE "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(gneiss_asset_pipeline_internal PRIVATE fastgltf::fastgltf)
target_compile_features(gneiss_asset_pipeline_internal PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_asset_pipeline_internal)

add_executable(gneiss_assetc main.cpp)
target_include_directories(gneiss_assetc PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_assetc PRIVATE gneiss_asset_pipeline_internal)
target_compile_features(gneiss_assetc PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_assetc)
gneiss_target_output_directories(gneiss_assetc)

if(GNEISS_BUILD_TESTING AND BUILD_TESTING)
  add_executable(
    gneiss_gltf_importer_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/gltf_importer_test.cpp"
  )
  target_include_directories(gneiss_gltf_importer_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_gltf_importer_test PRIVATE gneiss_asset_pipeline_internal)
  target_compile_definitions(
    gneiss_gltf_importer_test
    PRIVATE GNEISS_TEST_GLTF_ROOT="${PROJECT_SOURCE_DIR}/tests/data/gltf"
  )
  target_compile_features(gneiss_gltf_importer_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_gltf_importer_test)
  gneiss_target_output_directories(gneiss_gltf_importer_test)
  add_test(NAME gneiss.tooling.gltf_importer COMMAND gneiss_gltf_importer_test)

  add_test(
    NAME gneiss.tooling.assetc_inspect
    COMMAND gneiss_assetc inspect "${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf"
  )
  set_tests_properties(
    gneiss.tooling.assetc_inspect
    PROPERTIES PASS_REGULAR_EXPRESSION "场景=1 节点=1 网格=1 图元=1 材质=0 图像=0"
  )
endif()
