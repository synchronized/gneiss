# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 内部调度模块由实际宿主链接，不进入安装 package 或公共 ABI。
add_subdirectory(engine/core)

add_library(gneiss_engine)
add_library(gneiss::engine ALIAS gneiss_engine)
if(COMMAND gneiss_enable_asset_libuv)
  gneiss_enable_asset_libuv(gneiss_engine)
endif()
gneiss_target_profiling(gneiss_engine)
gneiss_target_output_directories(gneiss_engine)

set(GNEISS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(GNEISS_GENERATED_VERSION_HEADER
    "${GNEISS_GENERATED_INCLUDE_DIR}/engine/core/build_version.h")

file(MAKE_DIRECTORY "${GNEISS_GENERATED_INCLUDE_DIR}/engine/core")
configure_file(
  "${PROJECT_SOURCE_DIR}/src/engine/core/build_version.h.in"
  "${GNEISS_GENERATED_VERSION_HEADER}"
  @ONLY
)

target_sources(
  gneiss_engine
  PRIVATE
    engine/resource/asset_ref.hpp
    engine/resource/resource_retirement_queue.hpp
    engine/resource/asset_manager.hpp
    engine/resource/asset_manager.cpp
    engine/platform/files/file_read_stream.cpp
    engine/platform/files/file_service.cpp
    engine/platform/files/memory_file_backend.cpp
    engine/resource/files/async_read_file_backend.cpp
    engine/core/sha256.cpp
    engine/core/sha256.hpp
    runtime/loading/prefab_preparation.cpp
    runtime/loading/prefab_placement.cpp
    runtime/loading/scene_service_loading.cpp
    runtime/loading/scene_load_types.hpp
    runtime/loading/scene_service.cpp
    runtime/loading/scene_service.hpp
    runtime/loading/scene_domain.cpp
    runtime/loading/scene_domain.hpp
    runtime/application.cpp
    runtime/application.hpp
    runtime/main_loop_wakeup.hpp
    runtime/configuration/application_configuration.hpp
    engine/resource/vfs/asset_uri.cpp
    engine/resource/vfs/asset_uri.hpp
    engine/resource/types/asset_identity.hpp
    engine/resource/vfs/file_system.hpp
    engine/resource/loaders/mesh_binary.cpp
    engine/resource/loaders/mesh_binary.hpp
    engine/resource/loaders/texture_ktx2.cpp
    engine/resource/loaders/texture_ktx2.hpp
    engine/resource/loaders/texture_binary.cpp
    engine/resource/loaders/texture_binary.hpp
    engine/resource/loaders/texture_container.cpp
    engine/resource/loaders/texture_container.hpp
    engine/function/render/texture_load_service.cpp
    engine/function/render/texture_load_service.hpp
    engine/resource/vfs/native_file_system.cpp
    engine/resource/vfs/native_file_system.hpp
    engine/resource/cache/resource_cache.cpp
    engine/resource/cache/resource_cache.hpp
    engine/resource/vfs/virtual_file_system.cpp
    engine/resource/vfs/virtual_file_system.hpp
    engine/core/service_registry.cpp
    engine/core/service_registry.hpp
    engine/core/rid_table.hpp
    engine/core/identifier_conversion.hpp
    engine/core/guid.cpp
    engine/core/guid.hpp
    engine/core/string_name.cpp
    engine/core/string_name.hpp
    engine/core/value.hpp
    engine/core/traits/type_name.hpp
    engine/core/traits/type_identity.hpp
    engine/core/result.hpp
    engine/core/version.hpp

    engine/core/entity_id.hpp
    engine/core/meta/property.hpp
    engine/core/meta/type_info.hpp
    engine/function/world/component_identity.hpp

    engine/core/scene_types.hpp
    engine/core/type_id.cpp
    engine/core/type_id.hpp
    engine/core/detail/canonical_type_name.hpp
    engine/core/result.cpp
    engine/core/version.cpp
    engine/function/input/input_service.cpp
    engine/core/input/input_types.hpp
    engine/function/input/input_service.hpp
    engine/resource/types/action_map_validation.cpp
    engine/resource/loaders/scene_serialization.cpp
    engine/resource/loaders/scene_serialization.hpp
    engine/resource/loaders/action_map_serialization.cpp
    engine/resource/loaders/action_map_serialization.hpp
    engine/resource/loaders/action_map_parser.cpp
    engine/resource/types/action_map.hpp
    engine/core/log/log.hpp
    engine/core/log/log.cpp
    engine/core/log/log_dispatcher.cpp
    engine/core/log/log_dispatcher.hpp
    engine/core/log/log_types.hpp
    engine/function/render/render_resource.cpp
    engine/function/render/render_resource.hpp
    engine/function/render/render_resource_service.cpp
    engine/function/render/render_resource_service.hpp
    engine/function/render/texture_view.hpp
    engine/function/render/debug_draw_list.cpp
    engine/function/render/draw_types.hpp
    engine/function/render/debug_draw_list.hpp
    engine/function/render/ui_draw_list.cpp
    engine/function/render/ui_draw_list.hpp
    engine/function/render/render_asset_loader.cpp
    engine/resource/loaders/asset_preparation.cpp
    engine/function/render/render_asset_loader.hpp
    engine/resource/loaders/png_decoder.cpp
    engine/resource/loaders/png_decoder.hpp
    engine/core/math/transform.cpp
    engine/core/math/transform.hpp
    engine/core/math/camera.cpp
    engine/core/math/camera.hpp
    engine/function/render/render_snapshot.hpp
    engine/core/meta/type_registry.cpp
    engine/core/meta/serialization.hpp
    engine/resource/resource_registry.cpp
    engine/resource/resource_registry.hpp
    engine/resource/loaders/asset_decoder.hpp
    engine/resource/loaders/material_serialization.cpp
    engine/resource/loaders/material_serialization.hpp
    engine/resource/types/material_description.hpp
    engine/core/meta/type_builder.hpp
    engine/core/meta/type_registration.hpp
    engine/core/meta/type_registry.hpp
    engine/function/scene/scene_tree.cpp
    engine/function/scene/scene_tree.hpp
    engine/function/scene/scene_instantiation.cpp
    engine/function/scene/scene_load_preparation.cpp
    engine/function/scene/scene_load_preparation.hpp
    engine/resource/types/scene_description.cpp
    engine/resource/types/scene_description.hpp
    engine/function/scene/scene_creation.hpp
    engine/function/scene/scene_query.hpp
    engine/resource/types/prefab_description.cpp
    engine/resource/types/prefab_description.hpp
    engine/resource/types/prefab_property_data.cpp
    engine/function/scene/prefab_property_override.cpp
    engine/function/scene/prefab_property_override.hpp
    engine/function/scene/prefab_runtime_instance.cpp
    engine/function/scene/prefab_runtime_instance.hpp
    engine/function/scene/scene_instance.hpp
    engine/function/scene/scene_instances.cpp
    engine/function/scene/scene_instance_edit.cpp
    engine/function/scene/scene_instance_prefab.cpp
    engine/function/scene/scene_instances.hpp
    engine/function/scene/structural_diff.cpp
    engine/function/scene/structural_diff.hpp
    engine/function/world/system_scheduler.cpp
    engine/function/world/system_scheduler.hpp
    engine/function/world/render_snapshot.cpp
    engine/function/world/render_snapshot.hpp

    engine/function/world/world.cpp
    engine/function/world/world.hpp
    engine/function/world/world_types.hpp
    engine/function/world/world_factory.cpp
    engine/function/world/world_reflection.hpp
    engine/function/world/component_reflection.cpp
    engine/function/world/world_state.hpp

)

if(NOT GNEISS_ENABLE_GRANIT_PLATFORM)
  target_sources(gneiss_engine PRIVATE engine/function/render/render_service_headless.cpp)
endif()

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
      engine/function/render/render_service.cpp
      engine/function/render/render_service.hpp
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

target_compile_features(gneiss_engine PRIVATE cxx_std_20)
target_link_libraries(gneiss_engine PUBLIC $<BUILD_INTERFACE:gneiss_value_interface>)
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
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${GNEISS_GENERATED_INCLUDE_DIR}"
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

if(EMSCRIPTEN)
  target_sources(gneiss_engine PRIVATE
    engine/platform/files/browser/fetch_file_backend.cpp
    engine/platform/files/browser/browser_file_backend.cpp)
  target_link_options(gneiss_engine PUBLIC "-sFETCH=1" "-lidbfs.js")
endif()
