# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_assetc main.cpp)
target_include_directories(gneiss_assetc PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gneiss_assetc PRIVATE gneiss_asset_import_sdk)
target_compile_features(gneiss_assetc PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_assetc)
gneiss_target_output_directories(gneiss_assetc)

get_target_property(granit_asset_tools_imported granit::asset_tools IMPORTED)
get_target_property(granit_asset_tools_type granit::asset_tools TYPE)
if(WIN32 AND granit_asset_tools_imported AND granit_asset_tools_type STREQUAL "SHARED_LIBRARY")
  add_custom_command(
    TARGET gneiss_assetc POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:granit::asset_tools>"
            "$<TARGET_FILE_DIR:gneiss_assetc>"
    VERBATIM
  )
endif()
