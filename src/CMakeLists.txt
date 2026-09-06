# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(gneiss_engine)
add_library(gneiss::engine ALIAS gneiss_engine)
gneiss_target_output_directories(gneiss_engine)

set(GNEISS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(GNEISS_GENERATED_VERSION_HEADER
    "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/core/version.h")

file(MAKE_DIRECTORY "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/core")
configure_file(
  "${PROJECT_SOURCE_DIR}/include/gneiss/core/version.h.in"
  "${GNEISS_GENERATED_VERSION_HEADER}"
  @ONLY
)

target_sources(
  gneiss_engine
  PRIVATE
    application/application_api.cpp
    application/application_asset_reload_internal.h
    application/application_log_internal.h
    application/application_state.cpp
    application/application_state.h
    asset/asset_uri.cpp
    asset/asset_uri.h
    asset/file_system.h
    asset/mesh_binary.cpp
    asset/mesh_binary.h
    asset/native_file_system.cpp
    asset/native_file_system.h
    asset/resource_cache.cpp
    asset/resource_cache.h
    asset/virtual_file_system.cpp
    asset/virtual_file_system.h
    core/service_registry.cpp
    core/service_registry.h
    core/rid_table.h
    core/result.cpp
    core/version.cpp
    game/game_module.cpp
    game/game_context.cpp
    game/game_context_internal.h
    input/input_service.cpp
    input/input_service.h
    input/action_map.cpp
    input/action_map.h
    log/log.cpp
    log/log_dispatcher.cpp
    log/log_dispatcher.h
    render/render_resource_service.cpp
    render/render_resource_service.h
    render/debug_draw_list.cpp
    render/debug_draw_list.h
    render/ui_draw_list.cpp
    render/ui_draw_list.h
    render/render_asset_loader.cpp
    render/render_asset_loader.h
    render/png_decoder.cpp
    render/png_decoder.h
    render/camera_math.cpp
    render/camera_math.h
    render/normal_math.cpp
    render/normal_math.h
    reflection/type_registry.cpp
    scene/scene_tree.cpp
    scene/scene_tree.h
    scene/prefab_asset_loader.cpp
    scene/prefab_asset_loader.h
    scene/scene_description.cpp
    scene/scene_description.h
    scene/prefab_description.cpp
    scene/prefab_description.h
    scene/prefab_property_override.cpp
    scene/prefab_property_override.h
    scene/prefab_runtime_instance.cpp
    scene/prefab_runtime_instance.h
    scene/scene_instance_service.cpp
    scene/scene_instance_service.h
    scene/structural_diff.cpp
    scene/structural_diff.h
    world/system_scheduler.cpp
    world/system_scheduler.h
    world/render_snapshot.cpp
    world/render_snapshot.h
    world/world_api.cpp
    world/component_reflection.cpp
    world/world_state.h
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      "${PROJECT_SOURCE_DIR}/include"
      "${GNEISS_GENERATED_INCLUDE_DIR}"
    FILES
      "${PROJECT_SOURCE_DIR}/include/gneiss/application.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/application.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/asset.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/asset.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/export.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/entity.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/entity.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/result.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/result.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/rid.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/core/rid.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/gneiss.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/gneiss.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/game_module.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/game_module.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/input.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/input.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/log.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/log.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/reflection.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/reflection.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/render.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/render.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/scene.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/scene.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/world.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/world.hpp"
      "${GNEISS_GENERATED_VERSION_HEADER}"
)

if(GNEISS_ENABLE_GRANIT_PLATFORM)
  set(granit_pbr_asset_dir "${GNEISS_GRANIT_RENDER_PIPELINE_ASSET_DIR}/shaders/pbr")
  set(
    granit_pbr_assets
    pbr_standard.vert.grshader
    pbr_standard.vert.grshader.spv
    pbr_standard.vert.grshader.wgsl
    pbr_standard.frag.grshader
    pbr_standard.frag.grshader.spv
    pbr_standard.frag.grshader.wgsl
  )
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
        -P "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
      DEPENDS
        "${granit_pbr_asset_dir}/${granit_pbr_asset}"
        "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
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
      -P "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    DEPENDS "${granit_pbr_material_archive}" "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
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

  target_sources(
    gneiss_engine
    PRIVATE
      platform/granit/granit_platform.cpp
      platform/granit/granit_platform.h
      platform/native_window_info.h
      render/granit/granit_render_service.cpp
      render/granit/granit_render_service.h
      render/granit/scene_projection_math.cpp
      render/granit/scene_projection_math.h
      render/granit/pbr_shader_resolver.cpp
      render/granit/pbr_shader_resolver.h
      ${gneiss_pbr_embedded_sources}
      render/render_frame_packet.cpp
      render/render_frame_packet.h
      render/render_executor.cpp
      render/render_executor.h
  )
  target_compile_definitions(gneiss_engine PRIVATE GNEISS_HAS_GRANIT_PLATFORM)
  target_include_directories(gneiss_engine PRIVATE "${gneiss_pbr_embedded_dir}")
  target_link_libraries(
    gneiss_engine PRIVATE granit::granit granit::window granit::input granit::render_pipeline
  )
  add_custom_command(
    TARGET gneiss_engine
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:gneiss_engine>/assets/granit/shaders/pbr"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      ${granit_pbr_assets}
      "$<TARGET_FILE_DIR:gneiss_engine>/assets/granit/shaders/pbr"
    WORKING_DIRECTORY "${granit_pbr_asset_dir}"
    COMMAND_EXPAND_LISTS
    VERBATIM
  )

  get_target_property(granit_runtime_is_imported granit::granit IMPORTED)
  get_target_property(granit_runtime_type granit::granit TYPE)
  if(WIN32 AND granit_runtime_is_imported AND granit_runtime_type STREQUAL "SHARED_LIBRARY")
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
endif()

target_compile_features(gneiss_engine PUBLIC cxx_std_20)
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
  PRIVATE "${PROJECT_SOURCE_DIR}/src"
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${GNEISS_GENERATED_INCLUDE_DIR}>
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

if(GNEISS_BUILD_EDITOR OR GNEISS_BUILD_RUNTIME)
  add_subdirectory(io)
  add_subdirectory(ipc)
  add_subdirectory(platform)
endif()

if(GNEISS_BUILD_EDITOR)
  add_subdirectory(process)
endif()
