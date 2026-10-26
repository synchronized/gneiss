# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include(internal_headers)

# SDK 撤销期间的仓内共享值及转换辅助，不安装、不暴露实现目录。
gneiss_internal_headers(gneiss_value_interface "${PROJECT_SOURCE_DIR}/src"
  engine/core/math/functions.hpp
  engine/core/math/transform.hpp
  engine/core/math/types.hpp
  engine/core/export.hpp
  engine/core/result.hpp
  engine/core/version.hpp
  engine/core/log/log_types.hpp
  engine/core/guid.hpp
  engine/core/value.hpp

  engine/core/math/camera.hpp
  engine/core/entity_id.hpp
  engine/core/meta/property.hpp
  engine/core/meta/type_info.hpp
  engine/function/world/component_identity.hpp

  engine/core/scene_types.hpp
)

# 同源码宿主允许使用的引擎契约及其值类型闭包；不得添加后端、注册表实现或场景域存储。
gneiss_internal_headers(gneiss_native_interface "${PROJECT_SOURCE_DIR}/src"
  engine/platform/files/file_types.hpp
  engine/resource/types/asset_identity.hpp
  engine/resource/loaders/asset_preparation.hpp
  engine/resource/vfs/asset_uri.hpp
  engine/resource/vfs/file_system.hpp
  engine/resource/types/material_parameters.hpp
  engine/resource/types/material_types.hpp
  engine/resource/types/mesh_data.hpp
  engine/resource/vfs/read_source.hpp
  engine/resource/asset_ref.hpp
  engine/resource/resource_retirement_queue.hpp
  engine/resource/cache/resource_cache.hpp
  engine/resource/loaders/texture_ktx2.hpp
  engine/resource/types/texture_preparation_data.hpp
  engine/resource/types/texture_types.hpp
  engine/resource/vfs/virtual_file_system.hpp
  engine/core/diagnostics/diagnostic.hpp
  engine/core/diagnostics/loop_timing.hpp
  engine/core/frame_time.hpp
  engine/core/guid.hpp
  engine/core/string_name.hpp
  engine/core/value.hpp
  engine/core/traits/type_name.hpp
  engine/core/traits/type_identity.hpp
  engine/core/math/functions.hpp
  engine/core/math/transform.hpp
  engine/core/math/types.hpp
  engine/core/export.hpp
  engine/core/result.hpp
  engine/core/input/input_types.hpp
  engine/core/log/log_dispatcher.hpp
  engine/core/progress_notification.hpp
  engine/core/meta/serialization.hpp
  engine/resource/resource_registry.hpp
  engine/core/meta/type_registry.hpp
  engine/core/type_id.hpp
  engine/core/detail/canonical_type_name.hpp
  engine/core/rid.hpp
  engine/core/rid_table.hpp
  engine/core/sha256.hpp
  engine/core/tasks/task_scheduler.hpp
  runtime/application.hpp
  runtime/configuration/application_configuration.hpp
  runtime/loading/prefab_placement.hpp
  runtime/loading/prefab_preparation.hpp
  runtime/loading/scene_load_types.hpp
  engine/resource/types/action_map.hpp
  engine/function/input/input_service.hpp
  engine/core/math/camera.hpp
  engine/function/render/debug_draw_list.hpp
  engine/function/render/draw_types.hpp
  engine/function/render/render_asset_loader.hpp
  engine/function/render/render_asset_preparation.hpp
  engine/function/render/render_executor.hpp
  engine/function/render/render_frame_packet.hpp
  engine/function/render/render_resource.hpp
  engine/function/render/render_resource_data.hpp
  engine/function/render/render_resource_service.hpp
  engine/function/render/render_snapshot.hpp
  engine/function/render/render_statistics.hpp
  engine/function/render/texture_load_service.hpp
  engine/function/render/texture_view.hpp
  engine/function/render/ui_draw_list.hpp
  engine/resource/types/prefab_property_data.hpp
  engine/resource/types/prefab_description.hpp
  engine/function/scene/prefab_property_override.hpp
  engine/function/scene/scene_creation.hpp
  engine/function/scene/scene_mesh_binding.hpp
  engine/resource/types/scene_description.hpp
  engine/function/scene/scene_instances.hpp
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
  engine/core/math/functions.hpp
  engine/core/math/transform.hpp
  engine/core/math/types.hpp
  engine/core/export.hpp
  engine/core/result.hpp
  engine/resource/loaders/mesh_binary.hpp
  engine/resource/types/material_types.hpp
  tooling/asset_build/asset_build.hpp
  tooling/asset_build/ktx2_probe.hpp
  tooling/asset_build/runtime_texture_probe.hpp
  tooling/asset_import/asset_import_sdk.hpp
  tooling/asset_import/asset_index.hpp
  tooling/asset_import/asset_writer.hpp
  tooling/asset_import/gltf_importer.hpp
  tooling/asset_import/import_ir.hpp
)

target_link_libraries(gneiss_native_interface INTERFACE gneiss_value_interface)
target_link_libraries(gneiss_tooling_interface INTERFACE gneiss_value_interface)
