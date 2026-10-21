# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
set(fixture "${GNEISS_BINARY_DIR}/runtime-layout-v2-boundary-fixture")
file(MAKE_DIRECTORY "${fixture}/src/engine/function/render")
file(WRITE "${fixture}/src/engine/function/render/camera_math.hpp" "")
file(WRITE "${fixture}/src/engine/function/render/render_resource_data.hpp" "")
file(MAKE_DIRECTORY "${fixture}/src/runtime")
file(WRITE "${fixture}/src/runtime/loading/scene_session.cpp" "")
file(MAKE_DIRECTORY "${fixture}/src/engine/platform")
file(MAKE_DIRECTORY "${fixture}/src/editor")
file(WRITE "${fixture}/src/editor/probe.cpp" "")
file(MAKE_DIRECTORY "${fixture}/src/engine/api")
file(WRITE "${fixture}/src/engine/api/probe.cpp" "")
# 每次先恢复合法内容，避免上次中断的反例污染其他检查。
file(WRITE "${fixture}/src/engine/platform/probe.cpp" "#include <gneiss/core/result.hpp>\n")
file(MAKE_DIRECTORY "${fixture}/src/engine/core" "${fixture}/src/engine/function/world" "${fixture}/src/runtime" "${fixture}/src/engine/core/reflection" "${fixture}/src/engine/function/render" "${fixture}/src/engine/resource")
# 清除上一次失败用例遗留的内容，保证反例可重复执行。
file(WRITE "${fixture}/src/engine/resource/probe.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/src/engine/resource/loaders/asset_preparation.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/resource/loaders/asset_preparation.hpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/resource/loaders/png_decoder.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/core/reflection/probe.cpp" "#include <gneiss/reflection.h>\n")
file(WRITE "${fixture}/src/engine/function/world/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/src/runtime/probe.cpp" "#include <gneiss/application.h>\n")
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>" "#include <gneiss/engine/application.hpp>")
  file(WRITE "${fixture}/src/engine/core/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/core/probe.cpp" "#include <gneiss/core/result.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝共享值类型")
endif()

file(MAKE_DIRECTORY "${fixture}/src/engine/function/world")
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/world.hpp>"
    "auto result = gneiss_world_destroy(world);")
  file(WRITE "${fixture}/src/engine/function/world/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "World 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/world/probe.cpp" "#include <gneiss/render.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "World 边界检查错误拒绝共享值类型")
endif()

foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>" "#include <gneiss/engine/application.hpp>"
    "auto result = gneiss_application_destroy(application);")
  file(WRITE "${fixture}/src/runtime/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Application 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/runtime/probe.cpp" "#include <gneiss/application.h>\n")

file(WRITE "${fixture}/src/engine/function/world/probe.cpp" "auto result = gneiss_type_registry_freeze(registry);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝内部绕回 Reflection C ABI")
endif()
file(WRITE "${fixture}/src/engine/function/world/probe.cpp" "#include <gneiss/render.h>\n")

foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/reflection.hpp>")
  file(WRITE "${fixture}/src/engine/core/reflection/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Reflection 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/core/reflection/probe.cpp" "#include <gneiss/reflection.h>\n")

foreach(layer IN ITEMS world scene application)
  file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "#include \"${layer}/state.hpp\"\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Render 反向依赖 ${layer}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/src/engine/resource/loaders/png_decoder.cpp" "#include \"render/resource.hpp\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 PNG 解码依赖 Render")
endif()
file(WRITE "${fixture}/src/engine/resource/loaders/png_decoder.cpp" "#include <gneiss/core/result.h>\n")

foreach(layer IN ITEMS application world scene render resource apps)
  file(WRITE "${fixture}/src/engine/platform/probe.cpp" "#include \"${layer}/state.hpp\"\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Platform 反向依赖 ${layer}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/platform/probe.cpp" "#include <gneiss/core/result.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝 Platform 共享值类型")
endif()

foreach(header IN ITEMS "application/state.hpp" "engine/platform/native_window_info.hpp"
    "engine/function/world/state.hpp" "resource/cache.hpp" "apps/editor/session.hpp"
    "gneiss/application.h" "gneiss/engine/application.h")
  file(WRITE "${fixture}/src/engine/core/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Core 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/core/probe.cpp" "#include <gneiss/core/result.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝 Core 基础值类型")
endif()

foreach(header IN ITEMS "render/resource.hpp" "world/state.hpp" "scene/tree.hpp"
    "application/state.hpp" "engine/function/render/service.hpp" "apps/editor/session.hpp")
  file(WRITE "${fixture}/src/engine/resource/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Asset 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/resource/probe.cpp" "#include <gneiss/core/result.h>\n")

foreach(header IN ITEMS "engine/function/render/render_asset_loader.hpp" "engine/function/render/render_resource_service.hpp"
    "engine/resource/cache/resource_cache.hpp" "engine/core/rid_table.hpp")
  file(WRITE "${fixture}/src/engine/resource/loaders/asset_preparation.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 CPU 准备包含 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/resource/loaders/asset_preparation.cpp" "#include <gneiss/core/result.h>\n")

file(WRITE "${fixture}/src/engine/resource/loaders/asset_preparation.hpp" "#include <engine/function/render/render_resource_data.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝准备契约包含 Render 资源值")
endif()
file(WRITE "${fixture}/src/engine/resource/loaders/asset_preparation.hpp" "#include <gneiss/core/result.h>\n")

# 新路径反例：避免目录迁移后检查只匹配旧路径而静默失效。
foreach(header IN ITEMS "engine/function/world/state.hpp" "engine/function/scene/tree.hpp"
    "runtime/state.hpp" "apps/editor/session.hpp" "editor/session.hpp"
    "tooling/import.hpp")
  file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Function Render 依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "#include <gneiss/render.h>\n")
foreach(header IN ITEMS "runtime/state.hpp" "engine/function/render/service.hpp")
  file(WRITE "${fixture}/src/engine/platform/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Platform 依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/platform/probe.cpp" "#include <gneiss/core/result.h>\n")

# C 入口允许位于适配层，但不得借此绕过 Engine 的宿主依赖限制。
file(MAKE_DIRECTORY "${fixture}/src/engine/api")
foreach(case IN ITEMS valid apps editor tooling)
  if(case STREQUAL "valid")
    set(content "extern \"C\" void gneiss_probe() {}")
  else()
    set(content "#include <${case}/probe.hpp>")
  endif()
  file(WRITE "${fixture}/src/engine/api/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(case STREQUAL "valid" AND NOT status EQUAL 0)
    message(FATAL_ERROR "边界检查错误拒绝 C 适配入口")
  elseif(NOT case STREQUAL "valid" AND status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 C 适配层包含 ${case}")
  endif()
endforeach()

file(WRITE "${fixture}/src/engine/api/probe.cpp" "")
# 编辑器允许消费 SDK；源码迁入 src 不意味着成为 Engine 内部模块。
foreach(header IN ITEMS "gneiss/application.hpp" "apps/editor/main.hpp" "../../apps/editor/main.hpp"
    "gneiss/app/runtime_log_protocol.h" "ipc_inspection_protocol.h"
    "gneiss/app/runtime_log_protocol.hpp" "ipc_inspection_protocol.hpp")
  file(WRITE "${fixture}/src/editor/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(header STREQUAL "gneiss/application.hpp" AND NOT status EQUAL 0)
    message(FATAL_ERROR "边界检查错误拒绝 Editor 使用 SDK")
  elseif(NOT header STREQUAL "gneiss/application.hpp" AND status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Editor 包含 Apps：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/editor/probe.cpp" "")

file(WRITE "${fixture}/src/engine/platform/probe.cpp" "void initialize(const gneiss_application_desc&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Platform 消费 Application ABI 描述")
endif()
file(WRITE "${fixture}/src/engine/platform/probe.cpp" "void initialize(const window_configuration&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝 Platform 语义配置")
endif()

file(WRITE "${fixture}/src/runtime/probe.cpp" "void create(const gneiss_application_desc&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Application 核心消费 ABI 描述")
endif()
file(WRITE "${fixture}/src/runtime/probe.cpp" "")

foreach(content IN ITEMS "#include <engine/api/material_description.hpp>"
    "void create(const gneiss_material_desc&);"
    "void create(const gneiss_mesh_desc&);"
    "void create(const gneiss_texture_desc&);"
    "void submit(const gneiss_ui_draw_list_desc&);"
    "void submit(const gneiss_debug_draw_list_desc&);")
  file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Render 反向消费 ABI：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "")

file(MAKE_DIRECTORY "${fixture}/src/engine/function/scene")
foreach(type IN ITEMS node mesh_renderer_node prefab_instance camera)
  file(WRITE "${fixture}/src/engine/function/scene/probe.cpp"
    "void create(const gneiss_scene_${type}_desc&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Scene 消费 C 创建描述：${type}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/scene/probe.cpp" "")

foreach(type IN ITEMS instance prefab)
  file(WRITE "${fixture}/src/engine/function/scene/probe.cpp"
    "void query(gneiss_scene_${type}_node_info&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Scene 消费 C 查询描述：${type}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/scene/probe.cpp" "")

file(WRITE "${fixture}/src/engine/function/scene/probe.cpp"
  "void restore(const gneiss_scene_uuid_mapping*);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Scene 消费 C UUID 映射")
endif()
file(WRITE "${fixture}/src/engine/function/scene/probe.cpp" "")

foreach(module IN ITEMS application game)
  file(MAKE_DIRECTORY "${fixture}/src/runtime/${module}")
  file(WRITE "${fixture}/src/runtime/${module}/probe.cpp"
    "void log(const gneiss_log_message&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝内部 C 日志描述：${module}")
  endif()
  file(WRITE "${fixture}/src/runtime/${module}/probe.cpp" "")
endforeach()

# 协调层只接收上传接口，不能自行装配平台和 GPU 后端。
foreach(header IN ITEMS engine/platform/granit/granit_platform.hpp
                        engine/function/render/backend/granit/granit_render_service.hpp
                        granit/granit.hpp)
  file(WRITE "${fixture}/src/runtime/loading/scene_session.cpp"
    "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝协调层直接依赖后端：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/runtime/loading/scene_session.cpp"
  "#include <engine/function/render/texture_load_service.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝协调层依赖上传服务接口")
endif()

# 日志模块只允许原生接口及基础值类型。
file(MAKE_DIRECTORY "${fixture}/src/engine/core/log")
foreach(content IN ITEMS "#include <gneiss/engine/log.h>"
    "#include <gneiss/engine/log.hpp>" "auto level = GNEISS_LOG_INFO;"
    "gneiss_log_message value;" "#include \"engine/api/log_validation.hpp\"")
  file(WRITE "${fixture}/src/engine/core/log/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "日志边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/core/log/probe.cpp" "#include <gneiss/engine/core/result.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "日志边界检查错误拒绝基础值类型")
endif()

file(MAKE_DIRECTORY "${fixture}/src/engine/function/input")
foreach(content IN ITEMS "#include <gneiss/engine/input.h>" "gneiss_input_event event;"
    "gneiss_action_state state;" "#include <engine/api/input_conversion.hpp>")
  file(WRITE "${fixture}/src/engine/function/input/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "输入边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/input/probe.cpp" "#include <engine/core/input/input_types.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "输入边界检查错误拒绝原生值类型")
endif()

# 原生快照不能重新引入场景 ABI 值或 Granit 数学依赖。
foreach(content IN ITEMS "gneiss_transform transform;" "gneiss_camera camera;"
    "#include <granit/render.h>" "#include \"engine/function/render/backend/granit/probe.hpp\"")
  file(WRITE "${fixture}/src/engine/function/render/camera_math.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生快照边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/render/camera_math.hpp" "")

foreach(content IN ITEMS "gneiss_texture texture;" "gneiss_mesh_vertex vertex;"
    "#include <gneiss/engine/render.h>" "auto mode = GNEISS_MATERIAL_ALPHA_BLEND;")
  file(WRITE "${fixture}/src/engine/function/render/render_resource_data.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生资源边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/render/render_resource_data.hpp" "")

foreach(content IN ITEMS "gneiss_camera value;" "gneiss_mesh_renderer value;"
    "#include <gneiss/engine/render.h>")
  file(WRITE "${fixture}/src/engine/function/world/world_components.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生组件边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/world/world_components.hpp" "")

file(MAKE_DIRECTORY "${fixture}/src/engine/function/scene")
foreach(content IN ITEMS "gneiss_transform value;" "auto value = GNEISS_TRANSFORM_IDENTITY;")
  file(WRITE "${fixture}/src/engine/function/scene/scene_tree.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生场景树边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/scene/scene_tree.hpp" "")

foreach(name IN ITEMS scene_query.hpp scene_creation.hpp scene_instance_service.cpp prefab_runtime_instance.cpp)
  file(WRITE "${fixture}/src/engine/function/scene/${name}" "gneiss_transform value;\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生场景消费者未拒绝 C TRS：${name}")
  endif()
  file(WRITE "${fixture}/src/engine/function/scene/${name}" "")
endforeach()

file(MAKE_DIRECTORY "${fixture}/src/editor")
foreach(content IN ITEMS "gneiss_scene_instance_load();" "scene_instance scene_;" "gneiss_world_entity_create();")
  file(WRITE "${fixture}/src/editor/documents/scene_document.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "编辑会话边界未拒绝 ABI 回退：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/editor/documents/scene_document.cpp" "")

foreach(content IN ITEMS "gneiss::sdk::world owner;" "gneiss_type_registry_create();")
  file(WRITE "${fixture}/src/editor/panels/property_inspector_model.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "编辑模型边界未拒绝 SDK/Registry 回退：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/editor/panels/property_inspector_model.cpp" "")

foreach(content IN ITEMS "gneiss_property_value value;" "auto kind = GNEISS_PROPERTY_KIND_VEC3;")
  file(WRITE "${fixture}/src/editor/panels/property_inspector_model.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生属性边界未拒绝 C 布局：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/editor/panels/property_inspector_model.cpp" "")

file(WRITE "${fixture}/src/editor/viewport/editor_grid.hpp" "#include \"../engine/function/render/draw_types.hpp\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "Editor 父目录越界未被拒绝")
endif()
file(WRITE "${fixture}/src/editor/viewport/editor_grid.hpp" "")

# 输入服务只使用资产值与运行时输入状态，不接管加载后端。
file(MAKE_DIRECTORY "${fixture}/src/engine/function/input")
foreach(header IN ITEMS "caors/assets/asset_manager.hpp" "engine/platform/caors/adapter.hpp"
    "engine/resource/backend/caors/caors_action_map.hpp" "engine/resource/vfs/virtual_file_system.hpp"
    "engine/resource/asset_manager.hpp")
  file(WRITE "${fixture}/src/engine/function/input/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "输入服务边界未拒绝资源后端：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/input/probe.cpp" "#include <engine/resource/types/action_map.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "输入服务边界错误拒绝纯资源值")
endif()

file(MAKE_DIRECTORY "${fixture}/src/engine/resource")
foreach(header IN ITEMS "engine/core/reflection/property_registry.hpp"
    "engine/function/scene/prefab_property_override.hpp" "engine/function/world/world.hpp")
  file(WRITE "${fixture}/src/engine/resource/types/scene_description.hpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "场景描述边界未拒绝运行时依赖：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/resource/types/scene_description.hpp" "#include <engine/resource/types/prefab_property_data.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "场景描述边界错误拒绝作者值")
endif()

file(MAKE_DIRECTORY "${fixture}/src/engine/resource")
foreach(symbol IN ITEMS texture_load_service texture_upload_backend texture_load_completion
    "render_internal::render_asset_loader" "render_internal::render_asset_lease")
  file(WRITE "${fixture}/src/engine/resource/asset_manager.hpp" "struct ${symbol};\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "资产管理边界未拒绝渲染通道协议：${symbol}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/resource/asset_manager.hpp" "")

# 旧目录即使没有反向 include，也不得重新出现源码。
foreach(old_path IN ITEMS "engine/asset" "engine/function/asset" "engine/function/application" "engine/function/game")
  file(MAKE_DIRECTORY "${fixture}/src/${old_path}")
  file(WRITE "${fixture}/src/${old_path}/legacy.hpp" "// obsolete path\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(REMOVE "${fixture}/src/${old_path}/legacy.hpp")
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝旧目录：${old_path}")
  endif()
endforeach()

# Engine 各层禁止依赖 Runtime，应用组装可以正向依赖 Engine。
foreach(layer IN ITEMS core platform resource function/scene api)
  file(MAKE_DIRECTORY "${fixture}/src/engine/${layer}")
  file(WRITE "${fixture}/src/engine/${layer}/runtime_probe.hpp" "#include <runtime/application.hpp>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(REMOVE "${fixture}/src/engine/${layer}/runtime_probe.hpp")
  if(status EQUAL 0)
    message(FATAL_ERROR "未拒绝 Engine ${layer} 反向依赖 Runtime")
  endif()
endforeach()
foreach(header IN ITEMS editor/documents/scene_document.hpp apps/editor/main.hpp tooling/asset_build.hpp)
  file(WRITE "${fixture}/src/runtime/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "未拒绝 Runtime 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/runtime/probe.cpp" "#include <engine/resource/asset_manager.hpp>\n")
file(MAKE_DIRECTORY "${fixture}/src/runtime/api")
file(WRITE "${fixture}/src/runtime/api/probe.cpp" "extern \"C\" int allowed();\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "错误拒绝 Runtime 正向组装或 C ABI 适配")
endif()

# Runtime 目录及对象须保持同一命名，不允许恢复旧别名或引用。
foreach(content IN ITEMS "namespace gneiss::application_internal {}" "namespace gneiss::game_internal {}" "gneiss::engine::application* app;")
  file(WRITE "${fixture}/src/runtime/namespace_probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Runtime 边界未拒绝旧命名空间：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/runtime/namespace_probe.cpp" "namespace gneiss::runtime {}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Runtime 边界错误拒绝新命名空间")
endif()
