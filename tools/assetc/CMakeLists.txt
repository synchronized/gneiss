# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_asset_import_sdk STATIC
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/mesh_binary.h"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_ktx2.h"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/asset/texture_binary.h"
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/render/png_decoder.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_index.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_index.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_import_sdk.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_import_sdk.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_writer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_writer.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/import_ir.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/asset_build.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/asset_build.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/bc7_encoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/bc7_encoder.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/ktx2_probe.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/ktx2_probe.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/runtime_texture_builder.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/runtime_texture_builder.h"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/sha256.h"
)
target_include_directories(
  gneiss_asset_import_sdk PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
  gneiss_asset_import_sdk
  PRIVATE fastgltf::fastgltf gneiss_bc7enc gneiss_yyjson gneiss_png granit::granit
)
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
    gneiss_asset_build_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/asset_build_test.cpp"
  )
  target_include_directories(gneiss_asset_build_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_asset_build_test PRIVATE gneiss_asset_import_sdk granit::granit)
  target_compile_features(gneiss_asset_build_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_asset_build_test)
  gneiss_target_output_directories(gneiss_asset_build_test)
  add_test(NAME gneiss.tooling.asset_build COMMAND gneiss_asset_build_test)

  add_executable(gneiss_bc7_encoder_test "${PROJECT_SOURCE_DIR}/tests/tooling/bc7_encoder_test.cpp")
  target_include_directories(gneiss_bc7_encoder_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_bc7_encoder_test PRIVATE gneiss_asset_import_sdk gneiss_bc7enc)
  target_compile_features(gneiss_bc7_encoder_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_bc7_encoder_test)
  gneiss_target_output_directories(gneiss_bc7_encoder_test)
  add_test(NAME gneiss.tooling.bc7_encoder COMMAND gneiss_bc7_encoder_test)

  add_executable(gneiss_sha256_test "${PROJECT_SOURCE_DIR}/tests/tooling/sha256_test.cpp")
  target_include_directories(gneiss_sha256_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_sha256_test PRIVATE gneiss_asset_import_sdk)
  target_compile_features(gneiss_sha256_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_sha256_test)
  gneiss_target_output_directories(gneiss_sha256_test)
  add_test(NAME gneiss.tooling.sha256 COMMAND gneiss_sha256_test)

  add_executable(
    gneiss_runtime_texture_builder_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/runtime_texture_builder_test.cpp"
  )
  target_include_directories(
    gneiss_runtime_texture_builder_test PRIVATE "${PROJECT_SOURCE_DIR}/src"
  )
  target_link_libraries(
    gneiss_runtime_texture_builder_test PRIVATE gneiss_asset_import_sdk granit::granit
  )
  target_compile_features(gneiss_runtime_texture_builder_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_runtime_texture_builder_test)
  gneiss_target_output_directories(gneiss_runtime_texture_builder_test)
  add_test(NAME gneiss.tooling.runtime_texture_builder COMMAND gneiss_runtime_texture_builder_test)

  add_executable(
    gneiss_ktx2_probe_test
    "${PROJECT_SOURCE_DIR}/tests/tooling/ktx2_probe_test.cpp"
  )
  target_include_directories(gneiss_ktx2_probe_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(gneiss_ktx2_probe_test PRIVATE gneiss_asset_import_sdk)
  target_compile_features(gneiss_ktx2_probe_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_ktx2_probe_test)
  gneiss_target_output_directories(gneiss_ktx2_probe_test)
  add_test(NAME gneiss.tooling.ktx2_probe COMMAND gneiss_ktx2_probe_test)

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
  add_test(
    NAME gneiss.tooling.assetc_cook
    COMMAND
      "${CMAKE_COMMAND}"
      "-DGNEISS_ASSETC=$<TARGET_FILE:gneiss_assetc>"
      "-DGNEISS_SOURCE=${PROJECT_SOURCE_DIR}/tests/data/gltf/static_triangle.gltf"
      "-DGNEISS_OUTPUT=${PROJECT_BINARY_DIR}/tests/assetc-cook"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_assetc_cook.cmake"
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
