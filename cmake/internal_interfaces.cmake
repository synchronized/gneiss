# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include(internal_headers)

# SDK 撤销期间的仓内共享值及转换辅助，不安装、不暴露实现目录。
gneiss_internal_headers(gneiss_value_interface "${PROJECT_SOURCE_DIR}/src"
  engine/core/math/aabb3.hpp
  engine/core/math/functions.hpp
  engine/core/math/transform.hpp
  engine/core/math/types.hpp
  engine/core/export.hpp
  engine/core/error.hpp
  engine/core/ref.hpp
  engine/core/result.hpp
  engine/core/version.hpp
  engine/core/log/log_types.hpp
  engine/core/guid.hpp
  engine/core/value.hpp
  engine/core/utf8.hpp

  engine/core/math/camera.hpp
  engine/core/entity_id.hpp
  engine/core/meta/property.hpp
  engine/core/meta/type.hpp
  engine/core/meta/serialization_types.hpp
  engine/core/type_id.hpp
  engine/core/detail/canonical_type_name.hpp
  engine/function/world/component_identity.hpp

  engine/function/scene/scene_types.hpp
)

# 同源码宿主允许使用的引擎契约及其值类型闭包；不得添加后端、注册表实现或场景域存储。
gneiss_internal_headers(gneiss_native_interface "${PROJECT_SOURCE_DIR}/src"
  engine/platform/files/file_service.hpp
  engine/platform/files/file_types.hpp
  engine/resource/asset_identity.hpp
  engine/resource/asset_diagnostic.hpp
  engine/resource/asset_decoder.hpp
  engine/function/resource/asset_request.hpp
  engine/function/resource/asset_loading.hpp
  engine/function/resource/asset_manager.hpp
  engine/function/resource/resource_request.hpp
  engine/function/resource/resource_service.hpp
  engine/function/resource/render_resource.hpp
  engine/resource/scene/component_description.hpp
  engine/resource/scene/scene_codec.hpp
  engine/resource/scene/prefab_codec.hpp
  engine/resource/asset_uri.hpp
  engine/resource/material/material_description.hpp
  engine/resource/material/material_codec.hpp
  engine/resource/mesh/mesh_data.hpp
  engine/resource/asset_ref.hpp
  engine/resource/resource_retirement_queue.hpp
  engine/function/resource/resource_cache.hpp
  engine/resource/texture/texture_ktx2.hpp
  engine/resource/texture/texture_data.hpp
  engine/resource/texture/texture_description.hpp
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
  engine/core/error.hpp
  engine/core/result.hpp
  engine/core/input/input_types.hpp
  engine/core/log/log_dispatcher.hpp
  engine/core/progress_notification.hpp
  engine/core/meta/property_path.hpp
  engine/core/meta/instance.hpp
  engine/core/meta/detail/type_access.hpp
  engine/core/meta/serialization.hpp
  engine/function/meta_registration.hpp
  engine/resource/builtin_resource_registration.hpp
  engine/core/meta/registry.hpp
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
  runtime/loading/resource_load_types.hpp
  runtime/loading/scene_load_types.hpp
  runtime/loading/scene_service.hpp
  engine/resource/input/action_map.hpp
  engine/function/input/input_service.hpp
  engine/core/math/camera.hpp
  engine/function/render/debug_draw_list.hpp
  engine/function/render/draw_types.hpp
  engine/function/render/render_executor.hpp
  engine/function/render/render_frame_packet.hpp
  engine/resource/mesh/mesh_resource.hpp
  engine/resource/texture/texture_resource.hpp
  engine/resource/material/material_resource.hpp
  engine/function/render/render_service.hpp
  engine/function/render/render_resource_data.hpp
  engine/function/render/render_snapshot.hpp
  engine/function/render/render_statistics.hpp
  engine/resource/texture/texture_view.hpp
  engine/function/render/ui_draw_list.hpp
  engine/resource/scene/prefab_property_data.hpp
  engine/resource/scene/prefab_description.hpp
  engine/function/scene/prefab_property_override.hpp
  engine/function/scene/scene_creation.hpp
  engine/function/scene/scene_subtree_snapshot.hpp
  engine/function/scene/scene_mesh_binding.hpp
  engine/resource/scene/scene_description.hpp
  engine/function/scene/component_adapter.hpp
  engine/function/scene/scene_instances.hpp
  engine/function/scene/scene_preparation.hpp
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
  engine/core/error.hpp
  engine/core/result.hpp
  engine/resource/mesh/mesh_binary.hpp
  engine/resource/texture/texture_description.hpp
  engine/resource/material/material_description.hpp
  tooling/asset_build/asset_build.hpp
  tooling/asset_build/ktx2_probe.hpp
  tooling/asset_build/runtime_texture_probe.hpp
  tooling/asset_import/asset_import_sdk.hpp
  tooling/asset_import/asset_index.hpp
  tooling/asset_import/asset_writer.hpp
  tooling/asset_import/gltf_importer.hpp
  tooling/asset_import/import_ir.hpp
)

target_link_libraries(gneiss_value_interface INTERFACE gneiss_math_dependency)
target_link_libraries(gneiss_native_interface INTERFACE gneiss_value_interface)
target_link_libraries(gneiss_tooling_interface INTERFACE gneiss_value_interface)
