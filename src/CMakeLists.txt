# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 内部调度模块由实际宿主链接，不进入安装 package 或公共 ABI。
add_subdirectory(engine/core)

add_library(gneiss_engine)
add_library(gneiss::engine ALIAS gneiss_engine)
target_link_libraries(gneiss_engine PRIVATE gneiss_fatal)
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
    engine/platform/files/file_read_stream.cpp
    engine/platform/files/file_service.cpp
    engine/platform/files/memory_file_backend.cpp
    engine/platform/window_service.cpp
    engine/platform/window_service.hpp
    engine/platform/native_window_info.hpp
    engine/platform/window_configuration.hpp
)

target_sources(
  gneiss_engine
  PRIVATE
    engine/core/sha256.cpp
    engine/core/sha256.hpp
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
    engine/core/error.hpp
    engine/core/ref.hpp
    engine/core/result.hpp
    engine/core/version.hpp
    engine/core/entity_id.hpp
    engine/core/type_id.cpp
    engine/core/type_id.hpp
    engine/core/error.cpp
    engine/core/version.cpp
    engine/core/detail/canonical_type_name.hpp
    engine/core/input/input_types.hpp
    engine/core/log/log.hpp
    engine/core/log/log.cpp
    engine/core/log/log_dispatcher.cpp
    engine/core/log/log_dispatcher.hpp
    engine/core/log/log_types.hpp
)

target_sources(
  gneiss_engine
  PRIVATE
    engine/core/meta/detail/type_ops.hpp
    engine/core/meta/ops/ops_fwd.hpp
    engine/core/meta/ops/scalar_ops.hpp
    engine/core/meta/ops/sequence_ops.hpp
    engine/core/meta/ops/associative_ops.hpp
    engine/core/meta/ops/optional_ops.hpp
    engine/core/meta/ops/variant_ops.hpp
    engine/core/meta/ops/reference_ops.hpp
    engine/core/meta/ops/object_ops.hpp
    engine/core/math/transform.cpp
    engine/core/math/transform.hpp
    engine/core/math/camera.cpp
    engine/core/math/camera.hpp
    engine/core/meta/registration.hpp
    engine/core/meta/metadata.hpp
    engine/core/property_edit_policy.hpp
    engine/core/meta/property.hpp
    engine/core/meta/type.hpp
    engine/core/meta/type_types.hpp
    engine/core/meta/type_definition.hpp
    engine/core/meta/registry.hpp
    engine/core/meta/instance.cpp
    engine/core/meta/object.cpp
    engine/core/meta/object.hpp
    engine/core/meta/object_type.hpp
    engine/core/meta/instance.hpp
    engine/core/meta/views/sequence_view.hpp
    engine/core/meta/views/associative_view.hpp
    engine/core/meta/views/optional_view.hpp
    engine/core/meta/views/variant_view.hpp
    engine/core/meta/views/reference_view.hpp
    engine/core/meta/registry.cpp
    engine/core/value.cpp
    engine/core/value_edit.cpp
    engine/core/serialization/builtin_registration.hpp
    engine/core/serialization/builtin_registration.cpp
    engine/core/serialization/math_registration.cpp
    engine/core/serialization/detail/json_adapter.hpp
    engine/core/serialization/detail/context_access.hpp
    engine/core/serialization/diagnostic.hpp
    engine/core/serialization/policy.hpp
    engine/core/serialization/context.cpp
    engine/core/serialization/context.hpp
    engine/core/serialization/serialization.cpp
    engine/core/serialization/serialization.hpp
    engine/core/serialization/value.cpp
    engine/core/serialization/traversal.cpp
    engine/core/serialization/reader.hpp
    engine/core/serialization/writer.hpp
    engine/core/serialization/detail/traversal.hpp
    engine/core/serialization/detail/typed_decode.hpp
    engine/core/serialization/json.cpp
    engine/core/serialization/json.hpp
)

target_sources(
  gneiss_engine
  PRIVATE
    engine/resource/asset_serialization.hpp
    engine/resource/asset.hpp
    engine/resource/asset_ref.hpp
    engine/resource/asset_uri.cpp
    engine/resource/asset_uri.hpp
    engine/resource/asset_identity.hpp
    engine/resource/resource_retirement_queue.hpp
    engine/resource/input/action_map.hpp
    engine/resource/input/action_map_validation.cpp
    engine/resource/mesh/detail/mesh_resource_access.hpp
    engine/resource/mesh/mesh_binary.cpp
    engine/resource/mesh/mesh_binary.hpp
    engine/resource/mesh/mesh_decoder.cpp
    engine/resource/mesh/mesh_description.cpp
    engine/resource/mesh.cpp
    engine/resource/scene.hpp
    engine/resource/prefab.hpp
    engine/resource/scene/detail/scene_asset_access.hpp
    engine/resource/mesh.hpp
    engine/resource/texture/detail/texture_resource_access.hpp
    engine/resource/texture/texture_ktx2.cpp
    engine/resource/texture/texture_ktx2.hpp
    engine/resource/texture/texture_binary.cpp
    engine/resource/texture/texture_binary.hpp
    engine/resource/texture/texture_decoder.cpp
    engine/resource/texture/texture_description.cpp
    engine/resource/texture.cpp
    engine/resource/texture.hpp
    engine/resource/texture/texture_view.hpp
    engine/resource/texture/png_decoder.cpp
    engine/resource/texture/png_decoder.hpp
    engine/resource/material/detail/material_resource_access.hpp
    engine/resource/material.cpp
    engine/resource/material/material_description.cpp
    engine/resource/material.hpp
    engine/resource/material/material_description.hpp
    engine/resource/scene/scene_validation.cpp
    engine/resource/scene/scene_validation.hpp
    engine/resource/scene/scene_record.hpp
    engine/resource/scene/scene_record.cpp
    engine/resource/scene/component_description.hpp
    engine/resource/scene/game_object_description.hpp
    engine/resource/scene/scene_description.hpp
    engine/resource/scene/prefab_description.hpp
    engine/resource/scene/prefab_property_data.cpp
    engine/resource/asset_file.cpp
    engine/resource/asset_file.hpp
    engine/resource/builtin_resource_registration.cpp
    engine/resource/builtin_resource_registration.hpp
    engine/resource/asset_decoder.hpp
)

target_sources(
  gneiss_engine
  PRIVATE
    engine/function/render/render_service.cpp
    engine/function/render/render_service.hpp
    engine/function/render/backend/granit/pbr_shader_resolver.cpp
    engine/function/render/backend/granit/pbr_shader_resolver.hpp
    engine/function/render/render_frame_packet.cpp
    engine/function/render/render_frame_packet.hpp
    engine/function/render/render_executor.cpp
    engine/function/render/render_executor.hpp
    engine/function/render/debug_draw_list.cpp
    engine/function/render/draw_types.hpp
    engine/function/render/debug_draw_list.hpp
    engine/function/render/ui_draw_list.cpp
    engine/function/render/ui_draw_list.hpp
    engine/function/render/render_service_resources.cpp
    engine/function/render/render_resource_storage.hpp
    engine/function/render/render_snapshot.hpp
    ${gneiss_pbr_embedded_sources}
)

target_sources(
  gneiss_engine
  PRIVATE

    engine/function/input/input_service.cpp
    engine/function/input/input_service.hpp
    engine/function/asset/asset_loader.hpp
    engine/function/asset/detail/loader_factory.hpp
    engine/function/asset/loaders/mesh_loader.cpp
    engine/function/asset/loaders/texture_loader.cpp
    engine/function/asset/loaders/scene_loader.cpp
    engine/function/asset/loaders/object_loader.cpp
    engine/function/asset/loaders/action_map_loader.cpp
    engine/function/asset/loaders/material_loader.cpp
    engine/function/asset/detail/asset_entry.hpp
    engine/function/asset/detail/load_operation.hpp
    engine/function/asset/detail/render_load_operation.hpp
    engine/function/asset/detail/render_load_operation.cpp
    engine/function/asset/detail/asset_decode.cpp
    engine/function/asset/detail/asset_construction.hpp
    engine/function/asset/detail/asset_construction.cpp
    engine/function/asset/asset_reference_context.hpp
    engine/function/asset/asset_manager.hpp
    engine/function/asset/asset_manager.cpp
    engine/function/asset/resource_cache.cpp
    engine/function/asset/resource_cache.hpp
    engine/function/scene/scene_types.hpp
    engine/function/scene/detail/hierarchy.cpp
    engine/function/scene/detail/hierarchy.hpp
    engine/function/scene/scene_application.cpp
    engine/function/scene/scene_application.hpp
    engine/function/scene/composition.cpp
    engine/function/scene/composition.hpp
    engine/resource/scene/object_address.hpp

    engine/function/scene/prefab_property_override.cpp
    engine/function/scene/prefab_property_override.hpp
    engine/function/scene/prefab_replay.cpp
    engine/function/scene/prefab_replay.hpp
    engine/function/scene/scene_instance.hpp
    engine/function/scene/component_registration.cpp
    engine/function/scene/scene_instance.cpp
    engine/function/scene/instantiation.cpp
    engine/function/scene/component_identity.hpp
    engine/function/scene/detail/system_scheduler.cpp
    engine/function/scene/detail/system_scheduler.hpp
    engine/function/scene/detail/render_extraction.cpp
    engine/function/scene/detail/render_extraction.hpp
    engine/function/scene/game_object.cpp
    engine/function/scene/world.cpp
    engine/function/scene/game_object.hpp
    engine/function/scene/world.hpp
    engine/function/scene/detail/world_factory.cpp
    engine/function/scene/detail/world_state.hpp
    engine/function/meta_registration.cpp
    engine/function/meta_registration.hpp
)

target_sources(
  gneiss_engine
  PRIVATE
    runtime/loading/scene_service_loading.cpp
    runtime/loading/scene_load_types.hpp
    runtime/loading/scene_service.cpp
    runtime/loading/scene_service.hpp
    runtime/application.cpp
    runtime/application.hpp
    runtime/main_loop_wakeup.hpp
    runtime/configuration/application_configuration.hpp
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
  # C++ 内部异常传播必须在编译与最终链接时同时启用。
  target_compile_options(gneiss_engine PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
  target_link_options(gneiss_engine PUBLIC -fexceptions)
  target_sources(gneiss_engine PRIVATE
    engine/platform/files/browser/fetch_stream.cpp
    engine/platform/files/browser/fetch_file_backend.cpp
    engine/platform/files/browser/browser_file_backend.cpp)
  target_link_options(gneiss_engine PUBLIC "-sFETCH=1" "-lidbfs.js"
    "$<BUILD_INTERFACE:SHELL:--js-library \"${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/files/browser/fetch_stream.js\">")
  set_property(TARGET gneiss_engine APPEND PROPERTY INTERFACE_LINK_DEPENDS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/files/browser/fetch_stream.js>")
endif()
