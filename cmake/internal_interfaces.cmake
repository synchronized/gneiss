# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include(internal_headers)

# 同源码宿主允许使用的引擎契约及其值类型闭包；不得添加后端、注册表实现或场景域存储。
gneiss_internal_headers(gneiss_native_interface "${PROJECT_SOURCE_DIR}/src"
  engine/asset/asset_identity.hpp
  engine/asset/asset_preparation.hpp
  engine/asset/file_system.hpp
  engine/asset/material_parameters.hpp
  engine/asset/material_types.hpp
  engine/asset/mesh_data.hpp
  engine/asset/read_source.hpp
  engine/asset/resource_cache.hpp
  engine/asset/texture_ktx2.hpp
  engine/asset/texture_preparation_data.hpp
  engine/asset/texture_types.hpp
  engine/asset/virtual_file_system.hpp
  engine/core/diagnostics/diagnostic.hpp
  engine/core/diagnostics/loop_timing.hpp
  engine/core/frame_time.hpp
  engine/core/input/input_types.hpp
  engine/core/log/log_dispatcher.hpp
  engine/core/math/transform.hpp
  engine/core/progress_notification.hpp
  engine/core/publication_group.hpp
  engine/core/reflection/property_registry.hpp
  engine/core/rid.hpp
  engine/core/rid_table.hpp
  engine/core/sha256.hpp
  engine/core/tasks/task_scheduler.hpp
  engine/function/application/application.hpp
  engine/function/application/application_bridge.hpp
  engine/function/application/application_configuration.hpp
  engine/function/application/prefab_placement.hpp
  engine/function/application/prefab_preparation.hpp
  engine/function/application/scene_load_types.hpp
  engine/function/game/game_context_internal.hpp
  engine/asset/action_map.hpp
  engine/function/input/input_service.hpp
  engine/function/render/camera_math.hpp
  engine/function/render/camera_settings.hpp
  engine/function/render/debug_draw_list.hpp
  engine/function/render/draw_types.hpp
  engine/function/render/render_asset_loader.hpp
  engine/function/render/render_asset_preparation.hpp
  engine/function/render/render_executor.hpp
  engine/function/render/render_frame_packet.hpp
  engine/function/render/render_resource_data.hpp
  engine/function/render/render_resource_service.hpp
  engine/function/render/render_snapshot.hpp
  engine/function/render/render_statistics.hpp
  engine/function/render/texture_load_service.hpp
  engine/function/render/texture_view.hpp
  engine/function/render/ui_draw_list.hpp
  engine/function/scene/prefab_asset_loader.hpp
  engine/asset/prefab_property_data.hpp
  engine/asset/prefab_description.hpp
  engine/function/scene/prefab_property_override.hpp
  engine/function/scene/prefab_candidate.hpp
  engine/function/scene/scene_creation.hpp
  engine/function/scene/scene_mesh_binding.hpp
  engine/asset/scene_description.hpp
  engine/function/scene/scene_instance_service.hpp
  engine/function/scene/scene_load_preparation.hpp
  engine/function/scene/scene_query.hpp
  engine/function/world/world.hpp
  engine/function/world/world_components.hpp
  engine/function/world/world_reflection.hpp
  engine/function/world/world_types.hpp
  engine/platform/native_window_info.hpp
  engine/platform/window_configuration.hpp
)

# 离线资产服务供 Editor 和命令行消费的契约。
gneiss_internal_headers(gneiss_tooling_interface "${PROJECT_SOURCE_DIR}/src"
  engine/asset/mesh_binary.hpp
  tooling/asset_build/asset_build.hpp
  tooling/asset_build/ktx2_probe.hpp
  tooling/asset_build/runtime_texture_probe.hpp
  tooling/asset_import/asset_import_sdk.hpp
  tooling/asset_import/asset_index.hpp
  tooling/asset_import/asset_writer.hpp
  tooling/asset_import/gltf_importer.hpp
  tooling/asset_import/import_ir.hpp
)
