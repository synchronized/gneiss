# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 基础模块独立构建；Engine 仅装配运行领域及其直接依赖。
add_subdirectory(src/core)
add_subdirectory(src/resource)
add_subdirectory(src/platform)
add_subdirectory(src/function/files)
add_subdirectory(src/function/ipc)

add_library(gneiss_engine)
add_library(gneiss::engine ALIAS gneiss_engine)
add_subdirectory(src/function)
target_link_libraries(gneiss_engine PRIVATE gneiss_libassert)
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  target_compile_definitions(gneiss_engine PRIVATE GNEISS_ASSET_LIBUV)
endif()
gneiss_target_profiling(gneiss_engine)
gneiss_target_output_directories(gneiss_engine)

set(GNEISS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(GNEISS_GENERATED_VERSION_HEADER
    "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/engine/core/build_version.h")

file(MAKE_DIRECTORY "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/engine/core")
configure_file(
  "${PROJECT_SOURCE_DIR}/engine/src/core/build_version.h.in"
  "${GNEISS_GENERATED_VERSION_HEADER}"
  @ONLY
)

target_sources(
  gneiss_engine
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src/core/version.cpp"
)

set(granit_pbr_asset_dir "${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/libraries")
set(granit_pbr_assets pbr_standard.grshlib)
if(NOT GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR)
  message(FATAL_ERROR "Granit Render Pipeline 未提供标准资产目录")
endif()
foreach(granit_pbr_asset IN LISTS granit_pbr_assets)
  if(NOT EXISTS "${granit_pbr_asset_dir}/${granit_pbr_asset}")
    message(FATAL_ERROR "缺少 Granit 标准 PBR 资产：${granit_pbr_asset_dir}/${granit_pbr_asset}")
  endif()
endforeach()
set(granit_pbr_material_archive
    "${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/materials/pbr_standard.grmat")
if(NOT EXISTS "${granit_pbr_material_archive}")
  message(FATAL_ERROR "缺少 Granit 标准 PBR Material：${granit_pbr_material_archive}")
endif()
set(gneiss_pbr_embedded_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/pbr")
set(gneiss_pbr_embedded_sources)
foreach(granit_pbr_asset IN LISTS granit_pbr_assets)
  set(gneiss_pbr_embedded_source "${gneiss_pbr_embedded_dir}/${granit_pbr_asset}.inc")
  add_custom_command(
    OUTPUT "${gneiss_pbr_embedded_source}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${gneiss_pbr_embedded_dir}"
    COMMAND
      "${CMAKE_COMMAND}" "-DINPUT=${granit_pbr_asset_dir}/${granit_pbr_asset}"
      "-DOUTPUT=${gneiss_pbr_embedded_source}"
      -P "${PROJECT_SOURCE_DIR}/cmake/scripts/embed_binary.cmake"
    DEPENDS
      "${granit_pbr_asset_dir}/${granit_pbr_asset}"
      "${PROJECT_SOURCE_DIR}/cmake/scripts/embed_binary.cmake"
    VERBATIM
  )
  list(APPEND gneiss_pbr_embedded_sources "${gneiss_pbr_embedded_source}")
endforeach()
set(gneiss_pbr_material_include "${gneiss_pbr_embedded_dir}/pbr_standard.grmat.inc")
add_custom_command(
  OUTPUT "${gneiss_pbr_material_include}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${gneiss_pbr_embedded_dir}"
  COMMAND
    "${CMAKE_COMMAND}" "-DINPUT=${granit_pbr_material_archive}"
    "-DOUTPUT=${gneiss_pbr_material_include}"
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/embed_binary.cmake"
  DEPENDS "${granit_pbr_material_archive}" "${PROJECT_SOURCE_DIR}/cmake/scripts/embed_binary.cmake"
  VERBATIM
)
add_custom_command(
  TARGET gneiss_engine
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "$<TARGET_FILE_DIR:gneiss_engine>/assets/granit/materials"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different "${granit_pbr_material_archive}"
    "$<TARGET_FILE_DIR:gneiss_engine>/assets/granit/materials"
  VERBATIM
)
list(APPEND gneiss_pbr_embedded_sources "${gneiss_pbr_material_include}")
target_sources(gneiss_engine PRIVATE ${gneiss_pbr_embedded_sources})
if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
  target_compile_definitions(gneiss_engine PRIVATE GNEISS_NATIVE_LOOP_WAIT)
endif()
target_include_directories(gneiss_engine PRIVATE "${gneiss_pbr_embedded_dir}")
target_link_libraries(
  gneiss_engine PRIVATE granit::granit granit::render_pipeline
)
add_custom_command(
  TARGET gneiss_engine
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "$<TARGET_FILE_DIR:gneiss_engine>/assets/granit/libraries"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    ${granit_pbr_assets}
    "$<TARGET_FILE_DIR:gneiss_engine>/assets/granit/libraries"
  WORKING_DIRECTORY "${granit_pbr_asset_dir}"
  COMMAND_EXPAND_LISTS
  VERBATIM
)
get_target_property(granit_runtime_is_imported granit::granit IMPORTED)
get_target_property(granit_runtime_type granit::granit TYPE)
if(WIN32 AND BUILD_SHARED_LIBS AND granit_runtime_is_imported AND granit_runtime_type STREQUAL "SHARED_LIBRARY")
  # Windows 不会沿链接信息查找 DLL；把已安装 package 的运行时依赖放到 Gneiss 同目录。
  add_custom_command(
    TARGET gneiss_engine
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:gneiss_engine>
      $<TARGET_FILE_DIR:gneiss_engine>
    COMMAND_EXPAND_LISTS
    VERBATIM
  )
endif()

target_compile_features(gneiss_engine PRIVATE cxx_std_20)
target_link_libraries(gneiss_engine PUBLIC gneiss_window gneiss_resource gneiss_files gneiss_logging gneiss_base gneiss_math gneiss_data gneiss_meta gneiss_serialization_json $<BUILD_INTERFACE:gneiss_events>)
gneiss_target_compile_warnings(gneiss_engine)
target_link_libraries(
  gneiss_engine
  PRIVATE
    $<BUILD_INTERFACE:EnTT::EnTT>
    $<BUILD_INTERFACE:gneiss_yyjson>
    $<BUILD_INTERFACE:gneiss_png>
)
target_include_directories(
  gneiss_engine
  PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/include>"
  PRIVATE "${PROJECT_SOURCE_DIR}/engine/src" "${GNEISS_GENERATED_INCLUDE_DIR}"
)
target_compile_definitions(
  gneiss_engine
  PRIVATE GNEISS_BUILDING_LIBRARY
  PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:GNEISS_STATIC_DEFINE>
)
set_target_properties(
  gneiss_engine
  PROPERTIES
    EXPORT_NAME engine
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)


if(EMSCRIPTEN)
  # C++ 内部异常传播必须在编译与最终链接时同时启用。
  target_compile_options(gneiss_engine PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
  target_link_options(gneiss_engine PUBLIC -fexceptions)
endif()
