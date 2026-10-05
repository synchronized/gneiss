# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 内部调度模块由实际宿主链接，不进入安装 package 或公共 ABI。
add_subdirectory(engine/core)

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
    engine/core/sha256.cpp
    engine/core/sha256.hpp
    api/c/application_api.cpp
    api/c/render_api.cpp
    api/c/scene_api.cpp
    api/c/input_api.cpp
    application/application_registry.cpp
    application/application_registry.hpp
    application/application_host.cpp
    application/application_asset_reload_internal.hpp
    application/application_log_internal.hpp
    application/application_log_sink.hpp
    application/scene_load_service.cpp
    application/scene_load_service.hpp
    application/application_scene_load_internal.hpp
    application/application_scene_state.cpp
    application/application_scene_state.hpp
    application/application_state.cpp
    application/application_state.hpp
    engine/asset/source_revision_file_system.cpp
    engine/asset/source_revision_file_system.hpp
    engine/asset/asset_uri.cpp
    engine/asset/asset_uri.hpp
    engine/asset/file_system.hpp
    engine/asset/mesh_binary.cpp
    engine/asset/mesh_binary.hpp
    engine/asset/texture_ktx2.cpp
    engine/asset/texture_ktx2.hpp
    engine/asset/texture_binary.cpp
    engine/asset/texture_binary.hpp
    engine/asset/texture_container.cpp
    engine/asset/texture_container.hpp
    render/texture_load_service.cpp
    render/texture_load_service.hpp
    engine/asset/native_file_system.cpp
    engine/asset/native_file_system.hpp
    engine/asset/resource_cache.cpp
    engine/asset/resource_cache.hpp
    engine/asset/virtual_file_system.cpp
    engine/asset/virtual_file_system.hpp
    engine/core/service_registry.cpp
    engine/core/service_registry.hpp
    engine/core/rid_table.hpp
    api/c/core_api.cpp
    engine/core/result.hpp
    engine/core/version.hpp
    engine/core/result.cpp
    engine/core/version.cpp
    api/c/game_module_api.cpp
    api/c/asset_api.cpp
    game/game_context.cpp
    game/game_context_internal.hpp
    input/input_service.cpp
    input/input_service.h
    input/action_map.cpp
    input/action_map.h
    api/c/log_api.cpp
    api/c/log_validation.hpp
    engine/core/log/log.hpp
    engine/core/log/log.cpp
    engine/core/log/log_dispatcher.cpp
    engine/core/log/log_dispatcher.hpp
    render/render_resource_service.cpp
    render/render_resource_service.h
    render/debug_draw_list.cpp
    render/debug_draw_list.h
    render/ui_draw_list.cpp
    render/ui_draw_list.h
    render/render_asset_loader.cpp
    render/render_asset_loader.h
    engine/asset/png_decoder.cpp
    engine/asset/png_decoder.hpp
    render/camera_math.cpp
    render/camera_math.h
    render/render_snapshot.hpp
    render/normal_math.cpp
    render/normal_math.h
    engine/core/reflection/type_registry.cpp
    engine/core/reflection/type_registry.hpp
    api/c/reflection_api.cpp
    scene/scene_tree.cpp
    scene/scene_tree.h
    scene/prefab_asset_loader.cpp
    scene/prefab_asset_loader.h
    scene/scene_load_builder.cpp
    scene/scene_load_builder.h
    scene/scene_load_preparation.cpp
    scene/scene_load_preparation.h
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
    world/system_scheduler.hpp
    world/render_snapshot.cpp
    world/render_snapshot.hpp
    api/c/world_api.cpp
    world/world_service.cpp
    world/world_service.hpp
    world/component_reflection.cpp
    world/world_state.hpp
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
      engine/platform/granit/granit_platform.cpp
      engine/platform/granit/granit_platform.hpp
      engine/platform/native_window_info.hpp
      render/backend/granit/granit_render_service.cpp
      render/backend/granit/granit_render_service.hpp
      render/backend/granit/scene_projection_math.cpp
      render/backend/granit/scene_projection_math.hpp
      render/backend/granit/pbr_shader_resolver.cpp
      render/backend/granit/pbr_shader_resolver.hpp
      ${gneiss_pbr_embedded_sources}
      render/render_frame_packet.cpp
      render/render_frame_packet.h
      render/render_executor.cpp
      render/render_executor.h
  )
  target_compile_definitions(gneiss_engine PRIVATE GNEISS_HAS_GRANIT_PLATFORM)
  target_include_directories(gneiss_engine PRIVATE "${gneiss_pbr_embedded_dir}")
  target_link_libraries(
    gneiss_engine PRIVATE granit::granit granit::window granit::render_pipeline
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
  add_subdirectory(engine/platform)
endif()
