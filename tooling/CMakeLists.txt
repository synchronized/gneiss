# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_tooling STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/import/asset_index.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/import/asset_index.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/import/asset_import_sdk.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/import/asset_import_sdk.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/import/asset_writer.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/import/asset_writer.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/import/gltf_importer.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/import/gltf_importer.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/import/import_ir.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/import/tangent_generation.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/import/tangent_generation.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/asset_build.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/build/asset_build.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/texture_mip_generator.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/texture_mip_generator.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/bc7_encoder.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/bc7_encoder.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/ktx2_probe.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/build/ktx2_probe.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/runtime_texture_builder.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/runtime_texture_builder.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/runtime_texture_probe.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gneiss/tooling/build/runtime_texture_probe.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/build/sha256.hpp"
)
target_include_directories(
  gneiss_tooling
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  gneiss_tooling
  PUBLIC gneiss::engine
  PRIVATE fastgltf::fastgltf gneiss_mikktspace gneiss_bc7enc gneiss_yyjson gneiss_png granit::granit granit::asset_tools
)
target_compile_features(gneiss_tooling PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_tooling)

add_library(gneiss::tooling ALIAS gneiss_tooling)
add_subdirectory(assetc)
