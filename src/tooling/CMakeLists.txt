# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_asset_import_sdk STATIC
  "${PROJECT_SOURCE_DIR}/src/engine/asset/mesh_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/mesh_binary.hpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/texture_ktx2.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/texture_ktx2.hpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/texture_binary.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/texture_binary.hpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/png_decoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/asset/png_decoder.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_index.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_index.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_import_sdk.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_import_sdk.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_writer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/asset_writer.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/gltf_importer.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/import_ir.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/tangent_generation.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_import/tangent_generation.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/asset_build.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/asset_build.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/texture_mip_generator.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/texture_mip_generator.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/bc7_encoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/bc7_encoder.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/ktx2_probe.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/ktx2_probe.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/runtime_texture_builder.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/runtime_texture_builder.hpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/runtime_texture_probe.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/runtime_texture_probe.hpp"
  "${PROJECT_SOURCE_DIR}/src/engine/core/sha256.cpp"
  "${PROJECT_SOURCE_DIR}/src/tooling/asset_build/sha256.hpp"
)
target_include_directories(
  gneiss_asset_import_sdk PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
  gneiss_asset_import_sdk
  PRIVATE fastgltf::fastgltf gneiss_mikktspace gneiss_bc7enc gneiss_yyjson gneiss_png granit::granit granit::asset_tools
)
target_compile_features(gneiss_asset_import_sdk PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_asset_import_sdk)
