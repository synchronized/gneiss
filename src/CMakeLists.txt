# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 内部调度模块由实际宿主链接，不进入安装 package 或公共 ABI。
add_subdirectory(engine/core)

add_library(gneiss_engine)
add_library(gneiss::engine ALIAS gneiss_engine)
gneiss_target_profiling(gneiss_engine)
gneiss_target_output_directories(gneiss_engine)

set(GNEISS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(GNEISS_GENERATED_VERSION_HEADER
    "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/engine/core/version.h")

file(MAKE_DIRECTORY "${GNEISS_GENERATED_INCLUDE_DIR}/gneiss/engine/core")
configure_file(
  "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/version.h.in"
  "${GNEISS_GENERATED_VERSION_HEADER}"
  @ONLY
)

target_sources(
  gneiss_engine
  PRIVATE
    engine/core/sha256.cpp
    engine/core/sha256.hpp
    engine/api/application_api.cpp
    engine/api/render_api.cpp
    engine/api/draw_description.hpp
    engine/api/material_description.hpp
    engine/api/mesh_description.hpp
    engine/api/texture_description.hpp
    engine/api/scene_api.cpp
    engine/api/scene_query_conversion.hpp
    engine/api/input_api.cpp
    engine/function/application/application_registry.cpp
    engine/function/application/application_registry.hpp
    engine/function/application/application_host.cpp
    engine/function/application/application_asset_reload_internal.hpp
    engine/function/application/application_log_internal.hpp
    engine/function/application/application_log_sink.hpp
    engine/function/application/scene_load_service.cpp
    engine/function/application/scene_load_service.hpp
    engine/function/application/application_scene_load_internal.hpp
    engine/function/application/application_scene_state.cpp
    engine/function/application/application_scene_state.hpp
    engine/function/application/application_state.cpp
    engine/function/application/application_state.hpp
    engine/function/application/application_configuration.hpp
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
    engine/function/render/texture_load_service.cpp
    engine/function/render/texture_load_service.hpp
    engine/asset/native_file_system.cpp
    engine/asset/native_file_system.hpp
    engine/asset/resource_cache.cpp
    engine/asset/resource_cache.hpp
    engine/asset/virtual_file_system.cpp
    engine/asset/virtual_file_system.hpp
    engine/core/service_registry.cpp
    engine/core/service_registry.hpp
    engine/core/rid_table.hpp
    engine/api/core_api.cpp
    engine/core/result.hpp
    engine/core/version.hpp
    engine/core/result.cpp
    engine/core/version.cpp
    engine/api/game_module_api.cpp
    engine/api/asset_api.cpp
    engine/function/game/game_context.cpp
    engine/function/game/game_context_internal.hpp
    engine/function/input/input_service.cpp
    engine/function/input/input_service.hpp
    engine/function/input/action_map.cpp
    engine/function/input/action_map.hpp
    engine/api/log_api.cpp
    engine/api/log_validation.hpp
    engine/core/log/log.hpp
    engine/core/log/log.cpp
    engine/core/log/log_dispatcher.cpp
    engine/core/log/log_dispatcher.hpp
    engine/function/render/render_resource_service.cpp
    engine/function/render/render_resource_service.hpp
    engine/function/render/texture_view.hpp
    engine/function/render/debug_draw_list.cpp
    engine/function/render/debug_draw_list.hpp
    engine/function/render/ui_draw_list.cpp
    engine/function/render/ui_draw_list.hpp
    engine/function/render/render_asset_loader.cpp
    engine/asset/asset_preparation.cpp
    engine/function/render/render_asset_loader.hpp
    engine/asset/png_decoder.cpp
    engine/asset/png_decoder.hpp
    engine/function/render/camera_math.cpp
    engine/function/render/camera_math.hpp
    engine/function/render/render_snapshot.hpp
    engine/function/render/normal_math.cpp
    engine/function/render/normal_math.hpp
    engine/core/reflection/type_registry.cpp
    engine/core/reflection/type_registry.hpp
    engine/api/reflection_api.cpp
    engine/function/scene/scene_tree.cpp
    engine/function/scene/scene_tree.hpp
    engine/function/scene/prefab_asset_loader.cpp
    engine/function/scene/prefab_asset_loader.hpp
    engine/function/scene/scene_load_builder.cpp
    engine/function/scene/scene_load_builder.hpp
    engine/function/scene/scene_load_preparation.cpp
    engine/function/scene/scene_load_preparation.hpp
    engine/function/scene/scene_description.cpp
    engine/function/scene/scene_description.hpp
    engine/function/scene/scene_creation.hpp
    engine/function/scene/scene_query.hpp
    engine/function/scene/prefab_description.cpp
    engine/function/scene/prefab_description.hpp
    engine/function/scene/prefab_property_override.cpp
    engine/function/scene/prefab_property_override.hpp
    engine/function/scene/prefab_runtime_instance.cpp
    engine/function/scene/prefab_runtime_instance.hpp
    engine/function/scene/scene_instance_service.cpp
    engine/function/scene/scene_instance_service.hpp
    engine/function/scene/structural_diff.cpp
    engine/function/scene/structural_diff.hpp
    engine/function/world/system_scheduler.cpp
    engine/function/world/system_scheduler.hpp
    engine/function/world/render_snapshot.cpp
    engine/function/world/render_snapshot.hpp
    engine/api/world_api.cpp
    engine/function/world/world_service.cpp
    engine/function/world/world_service.hpp
    engine/function/world/component_reflection.cpp
    engine/function/world/world_state.hpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      "${PROJECT_SOURCE_DIR}/include"
      "${GNEISS_GENERATED_INCLUDE_DIR}"
    FILES
      "${PROJECT_SOURCE_DIR}/include/gneiss/gneiss.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/gneiss.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/application.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/application.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/application.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/world.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/input.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/scene.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/render.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/reflection.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/game_module.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/detail/game_context.inl"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/asset.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/asset.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/game_module.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/game_module.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/input.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/input.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/log.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/log.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/reflection.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/reflection.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/render.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/render.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/scene.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/scene.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/world.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/world.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/entity.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/entity.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/export.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/result.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/result.hpp"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/rid.h"
      "${PROJECT_SOURCE_DIR}/include/gneiss/engine/core/rid.hpp"
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
      engine/platform/window_configuration.hpp
      engine/function/render/backend/granit/granit_render_service.cpp
      engine/function/render/backend/granit/granit_render_service.hpp
      engine/function/render/backend/granit/scene_projection_math.cpp
      engine/function/render/backend/granit/scene_projection_math.hpp
      engine/function/render/backend/granit/pbr_shader_resolver.cpp
      engine/function/render/backend/granit/pbr_shader_resolver.hpp
      ${gneiss_pbr_embedded_sources}
      engine/function/render/render_frame_packet.cpp
      engine/function/render/render_frame_packet.hpp
      engine/function/render/render_executor.cpp
      engine/function/render/render_executor.hpp
  )
  target_compile_definitions(gneiss_engine PRIVATE GNEISS_HAS_GRANIT_PLATFORM)
  if(GNEISS_TASK_THREADS AND NOT EMSCRIPTEN)
    target_compile_definitions(gneiss_engine PRIVATE GNEISS_NATIVE_LOOP_WAIT)
  endif()
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
