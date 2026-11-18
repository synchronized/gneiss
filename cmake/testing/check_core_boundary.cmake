# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED GNEISS_SOURCE_DIR)
  message(FATAL_ERROR "缺少 GNEISS_SOURCE_DIR")
endif()
# 只扫描自有模块，排除 tests、生成目录和第三方依赖。
set(sources)
foreach(root IN ITEMS engine/include engine/src editor runtime tooling project protocol)
  file(GLOB_RECURSE module_sources
    "${GNEISS_SOURCE_DIR}/${root}/*.cpp" "${GNEISS_SOURCE_DIR}/${root}/*.h"
    "${GNEISS_SOURCE_DIR}/${root}/*.hpp" "${GNEISS_SOURCE_DIR}/${root}/*.inl")
  list(APPEND sources ${module_sources})
endforeach()
if(NOT sources)
  message(FATAL_ERROR "内部模块 检查未找到源码，拒绝空检查")
endif()
foreach(source IN LISTS sources)
  file(READ "${source}" content)
  # include/src 同属一个领域；规则使用逻辑模块路径，诊断仍报告真实文件。
  set(source_file "${source}")
  file(RELATIVE_PATH source "${GNEISS_SOURCE_DIR}" "${source_file}")
  string(REGEX REPLACE "^engine/(include/gneiss/engine|src)/" "engine/" source "${source}")
  set(source "/${source}")
  # 契约 include 与私有 include 使用同一逻辑前缀参与边界判断。
  string(REGEX REPLACE "gneiss/engine/(core|resource|function|platform)/" [=[engine/\1/]=] content "${content}")
  string(REGEX REPLACE [=[([<"])(core|resource|function|platform)/]=] [=[\1engine/\2/]=] content "${content}")
  if(NOT source MATCHES "/engine/core/(meta|serialization)/" AND
      content MATCHES "(^|[^A-Za-z0-9_])default_registry[ \t\r\n]*[(]")
    message(FATAL_ERROR "Meta/Serialization 基础设施外部必须使用无 registry 入口，不能直接取得默认表：${source_file}")
  endif()
  if(source MATCHES "/engine/core/serialization/(traversal|serialization|reader|writer|policy|diagnostic)[.](hpp|cpp)$" AND
      content MATCHES "yyjson|serialization/(json[.]hpp|detail/json_adapter[.]hpp)|[.]json[.]")
    message(FATAL_ERROR "共享序列化遍历不得依赖 JSON 后端：${source_file}")
  endif()
  if(source MATCHES "/engine/core/meta/(registration|type_definition)[.]hpp$" AND
      content MATCHES "yyjson|json_serialization|json_bytes_serialization")
    message(FATAL_ERROR "Meta 类型注册不得包含 JSON 专用操作：${source_file}")
  endif()
  if(source MATCHES "/engine/core/meta/" AND
      content MATCHES "engine/core/serialization/|engine/(resource|function)/|editor/")
    message(FATAL_ERROR "Meta 不得依赖 Serialization、资产、功能层或编辑器：${source_file}")
  endif()
  if(content MATCHES "engine/resource/(mesh|texture|material)/detail/[a-z_]+_resource_access[.]hpp" AND
      NOT source MATCHES "/engine/(resource/(mesh|texture|material)|function/asset/(detail/asset_construction|asset_manager))[.]cpp$")
    message(FATAL_ERROR "资源私有访问器不得泄漏到普通调用方：${source_file}")
  endif()

  if(content MATCHES "engine/resource/scene/detail/scene_asset_access[.]hpp" AND
      NOT source MATCHES "/engine/function/asset/(loaders/scene_loader|asset_manager)[.]cpp$")
    message(FATAL_ERROR "场景资产私有访问器仅供类型加载器与资产管理器匿名创建使用：${source_file}")
  endif()

  if(source MATCHES "/engine/function/asset/detail/resource_load_group[.]hpp$" AND
      content MATCHES "service_[.](advance|collect_unused|stop|request_stop)[ \t\r\n]*[(]")
    message(FATAL_ERROR "资源批次只观察请求，不推进或关闭共享资产管理器：${source_file}")
  endif()

  # 应用装配只借用批次汇总，不允许包含其他资产私有实现。
  set(asset_boundary_content "${content}")
  if(source MATCHES "/engine/function/application/application[.]cpp$")
    string(REPLACE "engine/function/asset/detail/resource_load_group.hpp" ""
      asset_boundary_content "${asset_boundary_content}")
  endif()
  if(asset_boundary_content MATCHES "engine/function/asset/detail/" AND
      NOT source MATCHES "/engine/function/asset/")
    message(FATAL_ERROR "资产加载私有实现不得泄漏到普通调用方：${source_file}")
  endif()

  if(content MATCHES "engine/function/render/detail/frame_packet[.]hpp" AND
      NOT source MATCHES "/engine/function/render/")
    message(FATAL_ERROR "Render 帧消费数据不得泄漏到宿主或其他模块：${source_file}")
  endif()
  if(content MATCHES "engine/function/render/detail/executor[.]hpp" AND
      NOT source MATCHES "/engine/function/render/detail/(executor[.]cpp|service_impl[.]hpp)$")
    message(FATAL_ERROR "Render 执行器不得泄漏到普通调用方：${source_file}")
  endif()
  if(content MATCHES "engine/function/render/detail/(resource_storage|service_impl)[.]hpp" AND
      NOT source MATCHES "/engine/function/render/((service|service_resources|service_scenes)[.]cpp|detail/service_impl[.]hpp)$")
    message(FATAL_ERROR "Render 私有 RID 表不得泄漏到调用方：${source_file}")
  endif()
  if(source MATCHES "/engine/function/render/service(_resources|_scenes)?[.](cpp|hpp)$" AND
      content MATCHES "resource_bindings|render_resource_factory|resource_retirement_queue|resource_access")
    message(FATAL_ERROR "Render 服务不得反向管理上层资源对象：${source_file}")
  endif()

  if(content MATCHES "#[ \t]*include[ \t]*[<\"]caors/|::caors::")
    message(FATAL_ERROR "CAORS 已移除；资源使用普通 CPU 类型和宿主执行器：${source_file}")
  endif()
  if(content MATCHES "source_revision_file_system|open_read_for_validation|complete_validation")
    message(FATAL_ERROR "普通资源读取不得恢复来源账本或验证回执，请使用 open_read/begin_open_read：${source_file}")
  endif()
  # SDK 撤销期间的值转换层暂存旧边界适配；不安装，禁止建立运行时所有者。
  if(source MATCHES "/engine/values/")
    if(content MATCHES "class[ \t]+(application|world|scene_instance|type_registry)[ \t:{]")
      message(FATAL_ERROR "值转换层不得恢复对象 SDK：${source_file}")
    endif()
  endif()
  # 普通内部操作只使用原生结果；C ABI 与测试夹具保留显式边界。
  if(NOT source MATCHES "/(api|tests)/" AND NOT source MATCHES "/engine/values/" AND
      content MATCHES "(^|[^a-zA-Z0-9_])gneiss_result([^a-zA-Z0-9_]|$)|GNEISS_(SUCCESS|ERROR_[A-Z_]+)")
    message(FATAL_ERROR "内部操作须使用 gneiss::error 或 result<T>，整数转换只放在 ABI/协议边界：${source_file}")
  endif()
  # 命名空间表示功能领域，目录和 include 检查继续约束分层依赖。
  if(content MATCHES "gneiss::engine([^a-zA-Z0-9_]|$)")
    message(FATAL_ERROR "不得恢复 engine 命名空间中间层：${source_file}")
  endif()
  if(content MATCHES "namespace[ \t]+gneiss::(engine|runtime|editor|tooling|host)::(core|platform|resource|render|world|scene|input|game|api|asset_build|asset_import|runtime|editor|assetc)([^a-zA-Z0-9_]|$)")
    message(FATAL_ERROR "内部业务类型须使用两层命名空间；实现细节可用 detail：${source_file}")
  endif()
  if(content MATCHES "namespace[ \t]+gneiss::[a-z_]+_internal")
    message(FATAL_ERROR "不得恢复旧 internal 业务命名空间：${source_file}")
  endif()
  if(source MATCHES "/engine/(asset|function/(resource|game))/")
    message(FATAL_ERROR "旧模块目录已移除，请按职责使用 engine/function/asset、engine/resource 或 engine/function/application：${source_file}")
  endif()
  if(source MATCHES "/engine/function/render/service[.]hpp$" AND
      content MATCHES "granit/|granit::|granit_[A-Za-z_]|engine/function/render/backend/")
    message(FATAL_ERROR "渲染服务接口不得泄漏后端头文件或类型：${source_file}")
  endif()
  if(source MATCHES "/engine/" AND content MATCHES "#[ \t]*include[ \t]*[<\"](runtime|application)/")
    message(FATAL_ERROR "Engine 不得反向依赖 Runtime：${source_file}")
  endif()
  if(source MATCHES "/(runtime|application)/" AND content MATCHES "#[ \t]*include[ \t]*[<\"](editor|apps|host|tooling)/")
    message(FATAL_ERROR "Runtime 不得反向依赖宿主：${source_file}")
  endif()

  if(source MATCHES "/engine/resource/(material|mesh)/(material_description|material_source|material_document|mesh_document)[.]hpp$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]engine/(platform/|resource/(detail/|resource_cache|asset_manager|asset_decoder|backend/|[^/]+/[^/]*_(codec|decoder)[.]hpp))")
    message(FATAL_ERROR "CPU 资源类型不得依赖加载器、缓存或平台适配：${source_file}")
  endif()
  if(source MATCHES "/engine/resource/texture/texture_(data|types|description)[.]hpp$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](engine/resource/)?(detail/|texture/)?[^\">]*(codec|decoder|texture_ktx2|texture_binary)[.]hpp")
    message(FATAL_ERROR "纹理数据不得依赖格式编解码器：${source_file}")
  endif()
  if(source MATCHES "/engine/function/asset/resource_cache[.]" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]engine/(platform/|resource/backend/)")
    message(FATAL_ERROR "CPU 版本缓存不得依赖文件后端或加载器适配：${source_file}")
  endif()

  if(source MATCHES "/engine/resource/scene/(scene_description|prefab_description|prefab_property_data)[.](cpp|hpp)$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]engine/core/(meta/(registration|registry)|serialization/(json|serialization|reader|writer))[.]hpp")
    message(FATAL_ERROR "场景资产描述不得依赖运行时 Registry：${source_file}")
  endif()

  set(input_boundary_content "${content}")
  foreach(contract IN ITEMS input id)
    string(REPLACE "engine/platform/window/${contract}.hpp" "window_contract"
      input_boundary_content "${input_boundary_content}")
  endforeach()
  if(source MATCHES "/engine/function/input/" AND
      input_boundary_content MATCHES "#[ \t]*include[ \t]*[<\"](caors/|engine/platform/|engine/function/files/|engine/function/asset/asset_manager|engine/resource/(backend/|vfs/|asset_manager))")
    message(FATAL_ERROR "输入服务不得依赖资源管理器、文件后端或 CAORS：${source_file}")
  endif()

  if(source MATCHES "/engine/function/(scene/(scene_tree|scene_query|game_object|instantiation|scene_instance|scene_instances|scene_update|scene_instances_instantiation|world|world_types|detail/hierarchy|rendering/[^/]+))[.](hpp|cpp)$" AND
      content MATCHES "gneiss_transform|GNEISS_TRANSFORM_IDENTITY")
    message(FATAL_ERROR "原生场景树与快照不得使用 C TRS 布局：${source_file}")
  endif()
  if(source MATCHES "/engine/function/scene/(components|detail/world_impl)[.]hpp$" AND
      (content MATCHES "gneiss_(camera|mesh_renderer)|GNEISS_CAMERA_" OR
       content MATCHES "gneiss/engine/render[.]h"))
    message(FATAL_ERROR "原生 World 组件不得存储 C 渲染布局：${source_file}")
  endif()
  if(source MATCHES "/engine/(resource/(mesh|material|texture)/(mesh_data|material_description|texture_description|texture_data)|function/render/(render_resource_data|service|camera|render_asset_loader|texture_view))[.](hpp|cpp)$" AND
      (content MATCHES "gneiss_(mesh|material|texture)([^a-z_]|$)|gneiss_(mesh_vertex|mesh_normal|mesh_tangent|mesh_uv|mesh_color|texture_sampling)|GNEISS_(MATERIAL_ALPHA|TEXTURE_FILTER|TEXTURE_ADDRESS|TEXTURE_MIP|TEXTURE_FORMAT|TEXTURE_COLOR)_" OR
       content MATCHES "gneiss/engine/render[.]h"))
    message(FATAL_ERROR "原生资源接口不得依赖 Render C 值布局：${source_file}")
  endif()
  if(source MATCHES "/engine/(core/math/(camera|transform)|function/render/camera)[.](cpp|hpp)$" AND
      (content MATCHES "gneiss_(transform|camera)|GNEISS_(TRANSFORM|CAMERA)_" OR
       content MATCHES "engine/function/render/backend/|granit/"))
    message(FATAL_ERROR "通用渲染快照与数学不得依赖 C 场景值或后端：${source_file}")
  endif()
  if(source MATCHES "/engine/(core|function)/input/" AND
      (content MATCHES "gneiss_(input_event|keyboard_state|pointer_state|action_state)" OR
       content MATCHES "gneiss/(engine/)?input[.]h" OR
       content MATCHES "engine/api/"))
    message(FATAL_ERROR "原生输入模块不得反向依赖输入 ABI：${source_file}")
  endif()
  if(source MATCHES "/engine/core/meta/" AND
      (content MATCHES "gneiss_(type_registry|property_|field_|type_info|type_desc|type_id)" OR
       content MATCHES "gneiss/engine/reflection[.]h|engine/values/"))
    message(FATAL_ERROR "原生反射注册表不得依赖旧 C 属性布局、句柄表或转换层：${source_file}")
  endif()
  if(source MATCHES "/engine/core/logging/" AND
      (content MATCHES "gneiss_log_|GNEISS_LOG_" OR
       content MATCHES "gneiss/(engine/)?log[.]h" OR
       content MATCHES "engine/(api|function|platform)/"))
    message(FATAL_ERROR "原生日志模块不得依赖日志 ABI 或上层模块：${source_file}")
  endif()
  if(source MATCHES "/engine/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](apps|host|editor|tooling)/")
    message(FATAL_ERROR "Engine 不得反向包含宿主、编辑器或离线工具：${source_file}")
  endif()
  if(source MATCHES "/editor/documents/scene_document[.](cpp|hpp)$" AND
      content MATCHES "gneiss_(application|scene_instance|scene_node|world)_[a-z_]+[(]|(^|[ ;])scene_instance scene_")
    message(FATAL_ERROR "原生场景文档不得绕行场景 ABI 或 SDK 拥有包装：${source_file}")
  endif()
  if(source MATCHES "/editor/" AND
      content MATCHES "sdk::|gneiss_type_registry_[a-z_]+[(]")
    message(FATAL_ERROR "编辑器内部不得使用 SDK 所有者或绕回 Registry C ABI：${source_file}")
  endif()
  if(source MATCHES "/editor/(documents/commands/author_property_edit|ui/panels/reflected_property_editor|ui/viewport/editor_rotation_math)[.](cpp|hpp)$" AND
      content MATCHES "gneiss_property_|GNEISS_PROPERTY_|engine/core/meta/registration[.]hpp|[.][.]/engine/")
    message(FATAL_ERROR "编辑属性链不得泄漏 C 属性布局或绕过模块契约：${source_file}")
  endif()
  if(source MATCHES "/editor/" AND content MATCHES "[.][.]/engine/")
    message(FATAL_ERROR "Editor 不得以父目录路径绕过模块接口：${source_file}")
  endif()
  # Editor 使用内部模块接口；值类型仍允许使用公开定义。
  if(source MATCHES "/editor/")
    if(content MATCHES "#[ \t]*include[ \t]*[<\"](\\.\\./)*(apps|host)/")
      message(FATAL_ERROR "Editor 实现不得反向包含 Apps：${source_file}")
    endif()
    if(content MATCHES "#[ \t]*include[ \t]*[<\"](gneiss/app/|ipc_[a-z_]+_protocol\\.(h|hpp)[>\"])")
      message(FATAL_ERROR "Editor 实现不得直接依赖宿主协议：${source_file}")
    endif()
    continue()
  endif()
  # C 适配仅豁免内部入口规则，仍遵守 Engine 对宿主的依赖方向。
  if(source MATCHES "/(engine|runtime)/api/")
    continue()
  endif()
  if(source MATCHES "(/engine/function/asset/loaders/(mesh_decoder|texture_decoder)|/engine/function/(asset/detail/asset_decode|render/render_resource_data))\\.(cpp|hpp)$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/function/)?render/(render_asset_loader|service)\\.(h|hpp)|engine/(function/asset/(resource_cache|asset_manager|detail/asset_construction)|core/rid_table)\\.hpp)[>\"]")
    message(FATAL_ERROR "CPU 准备与值类型不得包含资源发布、缓存或 RID 表：${source_file}")
  endif()
  if(source MATCHES "/engine/function/asset/loaders/(mesh_decoder|texture_decoder)[.](cpp|hpp)$" AND
      content MATCHES "engine/function/render/")
    message(FATAL_ERROR "CPU 解码契约不得依赖 Render 资源或服务：${source_file}")
  endif()
  if(source MATCHES "/engine/function/asset/loaders/png_decoder[.](cpp|hpp)$" AND
      content MATCHES "(engine/function/)?render/|granit/")
    message(FATAL_ERROR "PNG 字节解码不得依赖 Render：${source_file}")
  endif()
  if(source MATCHES "/engine/core/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/)?(platform|function|application|world|scene|render|resource|editor|tooling|apps)/|gneiss/(engine/)?application\\.h[>\"])")
    message(FATAL_ERROR "Core 不得依赖平台或上层实现及 Application 协议：${source_file}")
  endif()
  if(source MATCHES "/engine/platform/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/function/)?(application|world|scene|render|input|game)|engine/resource|resource|apps)/")
    message(FATAL_ERROR "Platform 不得反向包含上层实现：${source_file}")
  endif()
  if(source MATCHES "/(engine/platform|runtime|application)/" AND content MATCHES "gneiss_application_desc")
    message(FATAL_ERROR "Platform/Application 实现只能接收语义配置，不得消费 Application ABI 描述：${source_file}")
  endif()
  # 同步桥接属于 Scene；Render 不得反向读取 World。
  if(source MATCHES "/(engine/function/)?render/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](engine/function/)?(world|scene|application)/")
    message(FATAL_ERROR "Render 不得反向包含 World / Scene / Application：${source_file}")
  endif()
  if(source MATCHES "/engine/function/scene/rendering/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](granit/|engine/function/render/backend/|(engine/function/)?application/)")
    message(FATAL_ERROR "场景渲染桥接不得直接依赖 GPU 后端或 Application：${source_file}")
  endif()
  if(source MATCHES "/engine/resource/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/)?(function|render|world|scene|application|editor|tooling|apps)/)")
    message(FATAL_ERROR "Asset 不得反向包含功能层或宿主实现：${source_file}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]engine/api/")
    message(FATAL_ERROR "内部实现不得反向依赖 C 适配层：${source_file}")
  endif()
  if(source MATCHES "/engine/function/render/" AND content MATCHES "gneiss_(material|mesh|texture|ui_draw_list|debug_draw_list)_desc")
    message(FATAL_ERROR "Render 资源实现不得消费或生成版本化 C 描述：${source_file}")
  endif()
  if(source MATCHES "/engine/function/scene/" AND
      content MATCHES "gneiss_scene_((node|mesh_renderer_node|prefab_instance|camera)_desc|(instance|prefab)_node_info|uuid_mapping)")
    message(FATAL_ERROR "Scene 只能接收内部借用配置和视图，不得消费 C 创建或查询描述：${source_file}")
  endif()
  if(source MATCHES "/(runtime|application)/" AND content MATCHES "gneiss_log_message")
    message(FATAL_ERROR "Application/Game 内部日志只接收文本视图，不得消费 C 消息描述：${source_file}")
  endif()
  # 浏览器桥接只允许这三个私有 JS 导入声明；其他 C 链接声明或定义仍拒绝。
  set(abi_content "${content}")
  if(source MATCHES "/engine/function/files/backends/detail/fetch_stream\\.cpp$")
    string(REPLACE [=[extern "C" {
void gneiss_fetch_stream_open(const char*, void*, void (*)(void*, int, int));
void gneiss_fetch_stream_read(int, void*, std::size_t, void*,
                              void (*)(void*, int, std::size_t, int));
void gneiss_fetch_stream_close(int);
}]=] "" abi_content "${abi_content}")
  endif()
  if(abi_content MATCHES "extern[ \t]+\"C\"")
    message(FATAL_ERROR "内部模块 不得定义 C ABI 入口：${source_file}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]gneiss/(engine/)?(application|world|scene|resource|render|reflection|game_module|gneiss)\\.hpp[>\"]")
    message(FATAL_ERROR "内部模块 不得依赖公共 SDK 包装：${source_file}")
  endif()
  if(content MATCHES "gneiss_(world|scene_node|application|type_registry|game_context|asset_uri|log_message)_[a-z_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回 公共 C ABI：${source_file}")
  endif()
  if(content MATCHES "gneiss_(transform|camera)_type_id[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回公共类型 ID 入口：${source_file}")
  endif()
endforeach()
list(LENGTH sources source_count)
message(STATUS "模块边界检查通过：扫描 ${source_count} 个自有源码文件")
