# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 内部调度模块由实际宿主链接，不进入安装 package 或公共 ABI。
add_subdirectory(src/core)
add_subdirectory(src/platform)

add_library(gneiss_engine)
add_library(gneiss::engine ALIAS gneiss_engine)
target_link_libraries(gneiss_engine PRIVATE gneiss_libassert)
if(COMMAND gneiss_enable_asset_libuv)
  gneiss_enable_asset_libuv(gneiss_engine)
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
    "${PROJECT_SOURCE_DIR}/engine/src/platform/files/read_stream.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/platform/files/service.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/platform/files/backends/memory.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/platform/window/service.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/window/service.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/window/native_handle.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/window/surface_state.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/platform/window/configuration.hpp"
)

target_sources(
  gneiss_engine
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/sha256.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/service_registry.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/service_registry.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/rid_table.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/identifier_conversion.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/types/guid.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/types/string_name.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/data/value.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/traits/type_name.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/traits/type_identity.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/error.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/types/ref.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/result.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/version.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/types/entity_id.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/types/type_id.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/version.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/detail/canonical_type_name.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/input/types.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/logging/detail/async_queue.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/logging/record.hpp"
)

target_sources(
  gneiss_engine
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/type_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/ops_fwd.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/scalar_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/value_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/representation_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/sequence_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/associative_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/optional_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/variant_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/reference_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/ops/object_ops.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/math/transform.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/math/camera.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/registration.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/metadata.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/property_edit_policy.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/property.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/type.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/type_types.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/type_definition.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/registry.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/object.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/object_type.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/instance.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/views/sequence_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/capabilities/sequence.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/capabilities/container.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/capabilities/enumeration.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/capabilities/object.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/views/object_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/capabilities/representation.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/views/associative_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/views/optional_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/views/variant_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/views/reference_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/meta/transform_properties.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/builtin_registration.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/serialization/detail/json_adapter.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/serialization/detail/context_access.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/diagnostic.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/policy.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/context.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/serialization.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/reader.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/writer.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/core/serialization/detail/traversal.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/detail/typed_decode.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/core/serialization/json.hpp"
)

target_sources(
  gneiss_engine
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset_serialization.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset_ref.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/asset_uri.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset_uri.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset_identity.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/resource_retirement_queue.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/input/action_map.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/input/action_map_validation.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/mesh/detail/mesh_resource_access.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/mesh/mesh_binary.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/mesh/mesh_binary.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/mesh/mesh_decoder.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/mesh/mesh_description.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/mesh.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/mesh.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/detail/texture_resource_access.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/texture_ktx2.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/texture/texture_ktx2.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/texture_binary.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/texture/texture_binary.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/texture_decoder.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/texture_description.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/texture/texture_view.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture/png_decoder.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/texture/png_decoder.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/texture.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/texture.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/material/detail/material_resource_access.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/material/material_description.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/material/material_description.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/material.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/material.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/scene/scene_validation.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/scene_validation.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/scene_record.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/scene/scene_record.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/component_description.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/game_object_description.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/scene_description.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/prefab_description.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/scene/prefab_property_data.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/prefab.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/scene/detail/scene_asset_access.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/asset_file.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset_file.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/resource/builtin_resource_registration.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/builtin_resource_registration.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/asset_decoder.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/resource/scene/object_address.hpp"
)

target_sources(
  gneiss_engine
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/frame_packet.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/frame_packet.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/executor.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/executor.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/scene.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/resource_storage.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/detail/service_impl.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/backend/granit/backend.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/backend/granit/backend.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/backend/granit/pbr_shader_resolver.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/backend/granit/pbr_shader_resolver.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/service.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/render/service.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/render/frame_result.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/debug_draw_list.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/render/draw_types.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/render/debug_draw_list.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/ui_draw_list.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/render/ui_draw_list.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/service_scenes.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/render/service_resources.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/render/camera.hpp"
)

target_sources(
  gneiss_engine
  PRIVATE

    "${PROJECT_SOURCE_DIR}/engine/src/function/input/service.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/input/service.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/input/action.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/asset/asset_loader.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/loader_factory.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/mesh_loader.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/texture_loader.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/scene_loader.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/object_loader.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/action_map_loader.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/loaders/material_loader.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_entry.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/load_operation.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/render_load_operation.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/render_load_operation.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_decode.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_construction.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/detail/asset_construction.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/asset/asset_reference_context.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/asset/asset_manager.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/asset_manager.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/resource_cache.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/resource_cache.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/asset/asset_batch_request.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/asset/asset_batch_request.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/scene_types.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/hierarchy.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/camera_selection_state.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/object_state.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/scene_update.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/scene_update.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/composition.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/composition.hpp"

    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/prefab_property_override.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/prefab_property_override.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/prefab_replay.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/prefab_replay.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/scene_instance.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/component_registration.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/scene_instance.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/instantiation.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/system_scheduler.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/system_scheduler.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/game_object.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/world.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/camera_selection.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/camera_selection.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/game_object.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/rendering/world_render_binding.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/rendering/world_render_binding.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/rendering/render_camera.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/rendering/render_camera.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/world_changes.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/object_change.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/scene/world.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/world_factory.cpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/scene/detail/world_impl.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/meta_registration.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/meta_registration.hpp"
)

target_sources(
  gneiss_engine
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src/function/application/application.cpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/application/application.hpp"
    "${PROJECT_SOURCE_DIR}/engine/src/function/application/main_loop_wakeup.hpp"
    "${PROJECT_SOURCE_DIR}/engine/include/gneiss/engine/function/application/configuration.hpp"
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
target_link_libraries(gneiss_engine PUBLIC gneiss_logging gneiss_base gneiss_math gneiss_data gneiss_meta gneiss_serialization_json $<BUILD_INTERFACE:gneiss_events>)
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
  target_sources(gneiss_engine PRIVATE
    src/platform/files/backends/detail/fetch_stream.cpp
    src/platform/files/backends/fetch.cpp
    src/platform/files/backends/browser.cpp)
  target_link_options(gneiss_engine PUBLIC "-sFETCH=1" "-lidbfs.js"
    "$<BUILD_INTERFACE:SHELL:--js-library \"${CMAKE_CURRENT_SOURCE_DIR}/src/platform/files/backends/detail/fetch_stream.js\">")
  set_property(TARGET gneiss_engine APPEND PROPERTY INTERFACE_LINK_DEPENDS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/platform/files/backends/detail/fetch_stream.js>")
endif()
