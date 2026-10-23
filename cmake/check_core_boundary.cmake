# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED GNEISS_SOURCE_DIR)
  message(FATAL_ERROR "缺少 GNEISS_SOURCE_DIR")
endif()
# 所有内部模块适用；公共边界单独位于 engine/api 与 runtime/api。
file(GLOB_RECURSE sources "${GNEISS_SOURCE_DIR}/src/*.cpp"
                          "${GNEISS_SOURCE_DIR}/src/*.h"
                          "${GNEISS_SOURCE_DIR}/src/*.hpp")
if(NOT sources)
  message(FATAL_ERROR "内部模块 检查未找到源码，拒绝空检查")
endif()
foreach(source IN LISTS sources)
  file(READ "${source}" content)
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]caors/|::caors::")
    message(FATAL_ERROR "CAORS 已移除；资源使用普通 CPU 类型和宿主执行器：${source}")
  endif()
  # 宿主可以消费公共 ABI 与模块接口，但业务层不得依赖宿主实现。
  if(source MATCHES "/src/host/")
    file(READ "${source}" host_content)
    if(host_content MATCHES "#[ \t]*include[ \t]*[<\"](\\.\\./)*apps/")
      message(FATAL_ERROR "Host 不得反向依赖可执行入口：${source}")
    endif()
    continue()
  endif()
  if(source MATCHES "/src/engine/(asset|function/(asset|application|game))/")
    message(FATAL_ERROR "旧模块目录已移除，请按职责使用 engine/resource 或 runtime：${source}")
  endif()
  if(content MATCHES "gneiss::(child_process|child_process_start_info|dynamic_library|ipc_transport|ipc_envelope|ipc_dispatcher|ipc_domain_registry)([^a-zA-Z0-9_]|$)")
    message(FATAL_ERROR "平台对象须归属 gneiss::engine::platform：${source}")
  endif()
  if(content MATCHES "(log_internal|reflection_internal|io_internal|caors_internal|abi_internal|api_internal)(::|[ {;])|gneiss::(core|tasks|diagnostics|platform)(::|[ {;])")
    message(FATAL_ERROR "Core/Platform/ABI 辅助须使用所属模块命名空间，不得恢复旧名称：${source}")
  endif()
  if(content MATCHES "(world_internal|scene_internal|render_internal|input_internal)")
    message(FATAL_ERROR "功能层须使用 gneiss::engine 的领域命名空间，不得恢复旧 internal 名称：${source}")
  endif()
  if(source MATCHES "/src/engine/resource/" AND content MATCHES "engine::scene")
    message(FATAL_ERROR "资源描述与 Loader 须归属 engine::resource，不得回流 Scene 命名空间：${source}")
  endif()
  if(content MATCHES "asset_internal::|namespace[ \t]+gneiss::asset_internal|engine::(asset_manager|action_map_request|make_action_map_request)([^a-zA-Z0-9_]|$)")
    message(FATAL_ERROR "资源类型须使用 gneiss::engine::resource，不能恢复旧命名空间：${source}")
  endif()
  if(content MATCHES "(application_internal|game_internal)::|namespace[ \t]+gneiss::(application_internal|game_internal)|engine::application([^a-zA-Z0-9_]|$)")
    message(FATAL_ERROR "Runtime 类型须使用 gneiss::runtime，不能恢复旧业务命名空间：${source}")
  endif()
  if(source MATCHES "/src/engine/" AND content MATCHES "#[ \t]*include[ \t]*[<\"]runtime/")
    message(FATAL_ERROR "Engine 不得反向依赖 Runtime：${source}")
  endif()
  if(source MATCHES "/src/runtime/" AND content MATCHES "#[ \t]*include[ \t]*[<\"](editor|apps|host|tooling)/")
    message(FATAL_ERROR "Runtime 不得反向依赖宿主：${source}")
  endif()

  if(source MATCHES "/src/engine/resource/types/(material_source|material_document|mesh_document)[.]hpp$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]engine/(platform/|resource/(loaders/|cache/|backend/))")
    message(FATAL_ERROR "CPU 资源类型不得依赖加载器、缓存或平台适配：${source}")
  endif()
  if(source MATCHES "/src/engine/resource/cache/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]engine/(platform/|resource/backend/)")
    message(FATAL_ERROR "CPU 版本缓存不得依赖文件后端或加载器适配：${source}")
  endif()

  if(source MATCHES "/src/engine/resource/asset_manager[.](cpp|hpp)$" AND
      content MATCHES "texture_load_service|texture_upload_backend|texture_load_completion|engine::render::|engine/function/render/")
    message(FATAL_ERROR "资产管理器不得依赖渲染模块、上传通道或完成协议：${source}")
  endif()

  if(source MATCHES "/src/engine/resource/types/(scene_description|prefab_description|prefab_property_data)[.](cpp|hpp)$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]engine/core/reflection/(property_registry|type_registry|type_registry_state)[.]hpp")
    message(FATAL_ERROR "场景资产描述不得依赖运行时 Registry：${source}")
  endif()

  if(source MATCHES "/src/engine/function/input/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](caors/|engine/platform/|engine/resource/(backend/|vfs/|asset_manager))")
    message(FATAL_ERROR "输入服务不得依赖资源管理器、文件后端或 CAORS：${source}")
  endif()

  if(source MATCHES "/src/engine/function/(scene/(scene_tree|scene_query|scene_creation|scene_instance_service|prefab_runtime_instance|scene_instantiation)|world/(render_snapshot|world|world_types))[.](hpp|cpp)$" AND
      content MATCHES "gneiss_transform|GNEISS_TRANSFORM_IDENTITY")
    message(FATAL_ERROR "原生场景树与快照不得使用 C TRS 布局：${source}")
  endif()
  if(source MATCHES "/src/engine/function/world/(world_components|world_state)[.]hpp$" AND
      (content MATCHES "gneiss_(camera|mesh_renderer)|GNEISS_CAMERA_" OR
       content MATCHES "gneiss/engine/render[.]h"))
    message(FATAL_ERROR "原生 World 组件不得存储 C 渲染布局：${source}")
  endif()
  if(source MATCHES "/src/engine/(resource/types/(mesh_data|material_types|material_parameters|texture_types|texture_preparation_data)|function/render/(render_resource_data|render_resource_service|render_snapshot|render_asset_loader|texture_view))[.](hpp|cpp)$" AND
      (content MATCHES "gneiss_(mesh|material|texture)([^a-z_]|$)|gneiss_(mesh_vertex|mesh_normal|mesh_tangent|mesh_uv|mesh_color|texture_sampling)|GNEISS_(MATERIAL_ALPHA|TEXTURE_FILTER|TEXTURE_ADDRESS|TEXTURE_MIP|TEXTURE_FORMAT|TEXTURE_COLOR)_" OR
       content MATCHES "gneiss/engine/render[.]h"))
    message(FATAL_ERROR "原生资源接口不得依赖 Render C 值布局：${source}")
  endif()
  if(source MATCHES "/src/engine/function/render/(camera_math|scene_projection_math|camera_settings|render_snapshot)[.](cpp|hpp)$" AND
      (content MATCHES "gneiss_(transform|camera)|GNEISS_(TRANSFORM|CAMERA)_" OR
       content MATCHES "engine/function/render/backend/|granit/"))
    message(FATAL_ERROR "通用渲染快照与数学不得依赖 C 场景值或后端：${source}")
  endif()
  if(source MATCHES "/src/engine/(core|function)/input/" AND
      (content MATCHES "gneiss_(input_event|keyboard_state|pointer_state|action_state)" OR
       content MATCHES "gneiss/(engine/)?input[.]h" OR
       content MATCHES "engine/api/"))
    message(FATAL_ERROR "原生输入模块不得反向依赖输入 ABI：${source}")
  endif()
  if(source MATCHES "/src/engine/core/log/" AND
      (content MATCHES "gneiss_log_|GNEISS_LOG_" OR
       content MATCHES "gneiss/(engine/)?log[.]h" OR
       content MATCHES "engine/(api|function|platform)/"))
    message(FATAL_ERROR "原生日志模块不得依赖日志 ABI 或上层模块：${source}")
  endif()
  if(source MATCHES "/src/runtime/loading/scene_session\\.(cpp|hpp)$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](engine/platform/|engine/function/render/backend/|granit/)")
    message(FATAL_ERROR "场景生命周期协调不得直接依赖平台或渲染后端：${source}")
  endif()
  if(source MATCHES "/src/engine/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](apps|host|editor|tooling)/")
    message(FATAL_ERROR "Engine 不得反向包含宿主、编辑器或离线工具：${source}")
  endif()
  if(source MATCHES "/src/editor/documents/scene_document[.](cpp|hpp)$" AND
      content MATCHES "gneiss_(application|scene_instance|scene_node|world)_[a-z_]+[(]|(^|[ ;])scene_instance scene_")
    message(FATAL_ERROR "原生场景文档不得绕行场景 ABI 或 SDK 拥有包装：${source}")
  endif()
  if(source MATCHES "/src/editor/" AND
      content MATCHES "sdk::|gneiss_type_registry_[a-z_]+[(]")
    message(FATAL_ERROR "编辑器内部不得使用 SDK 所有者或绕回 Registry C ABI：${source}")
  endif()
  if(source MATCHES "/src/editor/(panels/property_inspector_model|commands/author_property_edit|panels/author_property_panel|viewport/editor_rotation_math)[.](cpp|hpp)$" AND
      content MATCHES "gneiss_property_|GNEISS_PROPERTY_|engine/core/reflection/type_registry[.]hpp|[.][.]/engine/")
    message(FATAL_ERROR "编辑属性链不得泄漏 C 属性布局或绕过模块契约：${source}")
  endif()
  if(source MATCHES "/src/editor/" AND content MATCHES "[.][.]/engine/")
    message(FATAL_ERROR "Editor 不得以父目录路径绕过模块接口：${source}")
  endif()
  # Editor 使用内部模块接口；值类型仍允许使用公开定义。
  if(source MATCHES "/src/editor/")
    if(content MATCHES "#[ \t]*include[ \t]*[<\"](\\.\\./)*(apps|host)/")
      message(FATAL_ERROR "Editor 实现不得反向包含 Apps：${source}")
    endif()
    if(content MATCHES "#[ \t]*include[ \t]*[<\"](gneiss/app/|ipc_[a-z_]+_protocol\\.(h|hpp)[>\"])")
      message(FATAL_ERROR "Editor 实现不得直接依赖宿主协议：${source}")
    endif()
    continue()
  endif()
  # C 适配仅豁免内部入口规则，仍遵守 Engine 对宿主的依赖方向。
  if(source MATCHES "/src/(engine|runtime)/api/")
    continue()
  endif()
  if(source MATCHES "(/src/engine/resource/loaders/asset_(preparation|parsing)|/src/(engine/function/)?render/(render_asset_preparation|render_resource_data))\\.(cpp|hpp)$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/function/)?render/(render_asset_loader|render_resource_service)\\.(h|hpp)|engine/(resource/cache/resource_cache|core/rid_table)\\.hpp)[>\"]")
    message(FATAL_ERROR "CPU 准备与值类型不得包含资源发布、缓存或 RID 表：${source}")
  endif()
  if(source MATCHES "/src/engine/core/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/)?(platform|function|application|world|scene|render|resource|editor|tooling|apps)/|gneiss/(engine/)?application\\.h[>\"])")
    message(FATAL_ERROR "Core 不得依赖平台或上层实现及 Application 协议：${source}")
  endif()
  if(source MATCHES "/src/engine/platform/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/function/)?(application|world|scene|render|input|game)|engine/resource|resource|apps)/")
    message(FATAL_ERROR "Platform 不得反向包含上层实现：${source}")
  endif()
  if(source MATCHES "/src/(engine/platform|runtime)/" AND content MATCHES "gneiss_application_desc")
    message(FATAL_ERROR "Platform/Application 实现只能接收语义配置，不得消费 Application ABI 描述：${source}")
  endif()
  if(source MATCHES "/src/(engine/function/)?render/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](engine/function/)?(world|scene|application)/")
    message(FATAL_ERROR "Render 不得反向包含 World / Scene / Application：${source}")
  endif()
  if(source MATCHES "/src/engine/resource/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/)?(function|render|world|scene|application|editor|tooling|apps)/)")
    message(FATAL_ERROR "Asset 不得反向包含功能层或宿主实现：${source}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]engine/api/")
    message(FATAL_ERROR "内部实现不得反向依赖 C 适配层：${source}")
  endif()
  if(source MATCHES "/src/engine/function/render/" AND content MATCHES "gneiss_(material|mesh|texture|ui_draw_list|debug_draw_list)_desc")
    message(FATAL_ERROR "Render 资源实现不得消费或生成版本化 C 描述：${source}")
  endif()
  if(source MATCHES "/src/engine/function/scene/" AND
      content MATCHES "gneiss_scene_((node|mesh_renderer_node|prefab_instance|camera)_desc|(instance|prefab)_node_info|uuid_mapping)")
    message(FATAL_ERROR "Scene 只能接收内部借用配置和视图，不得消费 C 创建或查询描述：${source}")
  endif()
  if(source MATCHES "/src/runtime/" AND content MATCHES "gneiss_log_message")
    message(FATAL_ERROR "Application/Game 内部日志只接收文本视图，不得消费 C 消息描述：${source}")
  endif()
  if(content MATCHES "extern[ \t]+\"C\"")
    message(FATAL_ERROR "内部模块 不得定义 C ABI 入口：${source}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]gneiss/(engine/)?(application|world|scene|resource|render|reflection|game_module|gneiss)\\.hpp[>\"]")
    message(FATAL_ERROR "内部模块 不得依赖公共 SDK 包装：${source}")
  endif()
  if(content MATCHES "gneiss_(world|scene_node|application|type_registry|game_context|asset_uri)_[a-z_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回 公共 C ABI：${source}")
  endif()
  if(content MATCHES "gneiss_(transform|camera)_type_id[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回公共类型 ID 入口：${source}")
  endif()
endforeach()
