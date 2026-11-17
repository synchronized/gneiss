# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef fixture_id)
set(fixture "${GNEISS_BINARY_DIR}/core-boundary-fixture-${fixture_id}")
file(WRITE "${fixture}/runtime/probe.cpp" "")
file(WRITE "${fixture}/engine/src/function/scene/rendering/probe.cpp" "")
file(WRITE "${fixture}/engine/src/function/render/backend/probe.cpp" "")
# 场景消费者反例中断后也必须恢复，避免污染下一次基线检查。
foreach(name IN ITEMS scene_instance.hpp game_object.hpp instantiation.cpp scene_update.cpp)
  file(WRITE "${fixture}/engine/src/function/scene/${name}" "")
endforeach()
# 恢复全部反例文件；前次执行留下的非法命名空间不能污染基线检查。
file(GLOB_RECURSE namespace_probes "${fixture}/*namespace_probe.cpp")
foreach(probe IN LISTS namespace_probes)
  file(WRITE "${probe}" "")
endforeach()
file(MAKE_DIRECTORY "${fixture}/engine/src/function/render")
file(WRITE "${fixture}/engine/src/core/math/camera.hpp" "")
file(WRITE "${fixture}/engine/src/function/render/render_resource_data.hpp" "")
file(MAKE_DIRECTORY "${fixture}/engine/src/platform")
file(MAKE_DIRECTORY "${fixture}/editor")
file(WRITE "${fixture}/editor/probe.cpp" "")
file(WRITE "${fixture}/engine/src/function/input/probe.cpp" "")
file(MAKE_DIRECTORY "${fixture}/engine/src/api")
file(WRITE "${fixture}/engine/src/api/probe.cpp" "")
# 每次先恢复合法内容，避免上次中断的反例污染其他检查。
file(WRITE "${fixture}/engine/src/platform/probe.cpp" "#include <gneiss/core/result.hpp>\n")
file(MAKE_DIRECTORY "${fixture}/engine/src/core" "${fixture}/engine/src/function/scene" "${fixture}/runtime" "${fixture}/engine/src/core/meta" "${fixture}/engine/src/function/render" "${fixture}/engine/src/resource")
# 清除上一次失败用例遗留的内容，保证反例可重复执行。
file(WRITE "${fixture}/engine/src/resource/probe.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/engine/src/function/asset/detail/asset_decode.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/engine/src/resource/texture/texture_decoder.hpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/engine/src/resource/texture/png_decoder.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/engine/src/core/meta/probe.cpp" "#include <gneiss/reflection.h>\n")
file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/runtime/probe.cpp" "#include <gneiss/application.h>\n")
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>" "#include <gneiss/engine/application.hpp>")
  file(WRITE "${fixture}/engine/src/core/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/core/probe.cpp" "#include <gneiss/core/result.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝共享值类型")
endif()

file(MAKE_DIRECTORY "${fixture}/engine/src/function/scene")
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/world.hpp>"
    "auto result = gneiss_world_destroy(world);")
  file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "World 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "#include <gneiss/render.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "World 边界检查错误拒绝共享值类型")
endif()

foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>" "#include <gneiss/engine/application.hpp>"
    "auto result = gneiss_application_destroy(application);")
  file(WRITE "${fixture}/runtime/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Application 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/runtime/probe.cpp" "#include <gneiss/application.h>\n")

file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "auto result = gneiss_type_registry_freeze(registry);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝内部绕回 Reflection C ABI")
endif()
file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "#include <gneiss/render.h>\n")

foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/reflection.hpp>")
  file(WRITE "${fixture}/engine/src/core/meta/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Reflection 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/core/meta/probe.cpp" "#include <gneiss/reflection.h>\n")

foreach(layer IN ITEMS world scene application)
  file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "#include \"${layer}/state.hpp\"\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Render 反向依赖 ${layer}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/engine/src/resource/texture/png_decoder.cpp" "#include \"render/resource.hpp\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 PNG 解码依赖 Render")
endif()
file(WRITE "${fixture}/engine/src/resource/texture/png_decoder.cpp" "#include <gneiss/core/result.h>\n")

foreach(layer IN ITEMS application world scene render resource apps)
  file(WRITE "${fixture}/engine/src/platform/probe.cpp" "#include \"${layer}/state.hpp\"\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Platform 反向依赖 ${layer}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/platform/probe.cpp" "#include <gneiss/core/result.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝 Platform 共享值类型")
endif()

foreach(header IN ITEMS "application/state.hpp" "engine/platform/native_window_info.hpp"
    "engine/function/scene/state.hpp" "resource/cache.hpp" "apps/editor/session.hpp"
    "gneiss/application.h" "gneiss/engine/application.h")
  file(WRITE "${fixture}/engine/src/core/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Core 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/core/probe.cpp" "#include <gneiss/core/result.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝 Core 基础值类型")
endif()

foreach(header IN ITEMS "render/resource.hpp" "world/state.hpp" "scene/tree.hpp"
    "application/state.hpp" "engine/function/render/service.hpp" "apps/editor/session.hpp")
  file(WRITE "${fixture}/engine/src/resource/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Asset 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/resource/probe.cpp" "#include <gneiss/core/result.h>\n")

foreach(header IN ITEMS "engine/function/asset/detail/asset_construction.hpp" "engine/function/asset/asset_manager.hpp" "engine/function/render/service.hpp"
    "engine/function/asset/resource_cache.hpp" "engine/core/rid_table.hpp")
  file(WRITE "${fixture}/engine/src/function/asset/detail/asset_decode.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 CPU 准备包含 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/asset/detail/asset_decode.cpp" "#include <gneiss/core/result.h>\n")

file(WRITE "${fixture}/engine/src/resource/texture/texture_decoder.hpp" "#include <engine/function/render/render_resource_data.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝准备契约包含 Render 资源值")
endif()
file(WRITE "${fixture}/engine/src/resource/texture/texture_decoder.hpp" "#include <gneiss/core/result.h>\n")

# 新路径反例：避免目录迁移后检查只匹配旧路径而静默失效。
foreach(header IN ITEMS "engine/function/scene/state.hpp" "engine/function/scene/tree.hpp"
    "runtime/state.hpp" "apps/editor/session.hpp" "editor/session.hpp"
    "tooling/import.hpp")
  file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Function Render 依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "#include <gneiss/render.h>\n")
foreach(header IN ITEMS "runtime/state.hpp" "engine/function/render/service.hpp")
  file(WRITE "${fixture}/engine/src/platform/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Platform 依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/platform/probe.cpp" "#include <gneiss/core/result.h>\n")

# C 入口允许位于适配层，但不得借此绕过 Engine 的宿主依赖限制。
file(MAKE_DIRECTORY "${fixture}/engine/src/api")
foreach(case IN ITEMS valid apps editor tooling)
  if(case STREQUAL "valid")
    set(content "extern \"C\" void gneiss_probe() {}")
  else()
    set(content "#include <${case}/probe.hpp>")
  endif()
  file(WRITE "${fixture}/engine/src/api/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(case STREQUAL "valid" AND NOT status EQUAL 0)
    message(FATAL_ERROR "边界检查错误拒绝 C 适配入口")
  elseif(NOT case STREQUAL "valid" AND status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 C 适配层包含 ${case}")
  endif()
endforeach()

file(WRITE "${fixture}/engine/src/api/probe.cpp" "")
# 编辑器允许消费 SDK；源码迁入 src 不意味着成为 Engine 内部模块。
foreach(header IN ITEMS "gneiss/application.hpp" "apps/editor/main.hpp" "../../apps/editor/main.hpp" "host/editor/application.hpp" "../../host/editor/application.hpp"
    "gneiss/app/runtime_log_protocol.h" "ipc_inspection_protocol.h"
    "gneiss/app/runtime_log_protocol.hpp" "ipc_inspection_protocol.hpp")
  file(WRITE "${fixture}/editor/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(header STREQUAL "gneiss/application.hpp" AND NOT status EQUAL 0)
    message(FATAL_ERROR "边界检查错误拒绝 Editor 使用 SDK")
  elseif(NOT header STREQUAL "gneiss/application.hpp" AND status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Editor 包含 Apps：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/editor/probe.cpp" "")
file(WRITE "${fixture}/engine/src/function/input/probe.cpp" "")

file(WRITE "${fixture}/engine/src/platform/probe.cpp" "void initialize(const gneiss_application_desc&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Platform 消费 Application ABI 描述")
endif()
file(WRITE "${fixture}/engine/src/platform/probe.cpp" "void initialize(const window_configuration&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝 Platform 语义配置")
endif()

file(WRITE "${fixture}/runtime/probe.cpp" "void create(const gneiss_application_desc&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Application 核心消费 ABI 描述")
endif()
file(WRITE "${fixture}/runtime/probe.cpp" "")

foreach(content IN ITEMS "#include <engine/api/result_conversion.hpp>"
    "void create(const gneiss_material_desc&);"
    "void create(const gneiss_mesh_desc&);"
    "void create(const gneiss_texture_desc&);"
    "void submit(const gneiss_ui_draw_list_desc&);"
    "void submit(const gneiss_debug_draw_list_desc&);")
  file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Render 反向消费 ABI：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "")

file(MAKE_DIRECTORY "${fixture}/engine/src/function/scene")
foreach(type IN ITEMS node mesh_renderer_node prefab_instance camera)
  file(WRITE "${fixture}/engine/src/function/scene/probe.cpp"
    "void create(const gneiss_scene_${type}_desc&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Scene 消费 C 创建描述：${type}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "")

foreach(type IN ITEMS instance prefab)
  file(WRITE "${fixture}/engine/src/function/scene/probe.cpp"
    "void query(gneiss_scene_${type}_node_info&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Scene 消费 C 查询描述：${type}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "")

file(WRITE "${fixture}/engine/src/function/scene/probe.cpp"
  "void restore(const gneiss_scene_uuid_mapping*);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Scene 消费 C UUID 映射")
endif()
file(WRITE "${fixture}/engine/src/function/scene/probe.cpp" "")

foreach(module IN ITEMS application game)
  file(MAKE_DIRECTORY "${fixture}/runtime/${module}")
  file(WRITE "${fixture}/runtime/${module}/probe.cpp"
    "void log(const gneiss_log_message&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝内部 C 日志描述：${module}")
  endif()
  file(WRITE "${fixture}/runtime/${module}/probe.cpp" "")
endforeach()

# 日志模块只允许原生接口及基础值类型。
file(MAKE_DIRECTORY "${fixture}/engine/src/core/log")
foreach(content IN ITEMS "#include <gneiss/engine/log.h>"
    "#include <gneiss/engine/log.hpp>" "auto level = GNEISS_LOG_INFO;"
    "gneiss_log_message value;" "#include \"engine/api/log_validation.hpp\"")
  file(WRITE "${fixture}/engine/src/core/logging/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "日志边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/core/logging/probe.cpp" "#include <gneiss/engine/core/result.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "日志边界检查错误拒绝基础值类型")
endif()

file(MAKE_DIRECTORY "${fixture}/engine/src/function/input")
foreach(content IN ITEMS "#include <gneiss/engine/input.h>" "gneiss_input_event event;"
    "gneiss_action_state state;" "#include <engine/api/result_conversion.hpp>")
  file(WRITE "${fixture}/engine/src/function/input/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "输入边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/input/probe.cpp" "#include <engine/core/input/input_types.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "输入边界检查错误拒绝原生值类型")
endif()

# 原生快照不能重新引入场景 ABI 值或 Granit 数学依赖。
foreach(content IN ITEMS "gneiss_transform transform;" "gneiss_camera camera;"
    "#include <granit/render.h>" "#include \"engine/function/render/backend/granit/probe.hpp\"")
  file(WRITE "${fixture}/engine/src/core/math/camera.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生快照边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/core/math/camera.hpp" "")

foreach(content IN ITEMS "gneiss_texture texture;" "gneiss_mesh_vertex vertex;"
    "#include <gneiss/engine/render.h>" "auto mode = GNEISS_MATERIAL_ALPHA_BLEND;")
  file(WRITE "${fixture}/engine/src/function/render/render_resource_data.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生资源边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/render_resource_data.hpp" "")

foreach(content IN ITEMS "gneiss_camera value;" "gneiss_mesh_renderer value;"
    "#include <gneiss/engine/render.h>")
  file(WRITE "${fixture}/engine/src/function/scene/components.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生组件边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/scene/components.hpp" "")

file(MAKE_DIRECTORY "${fixture}/engine/src/function/scene")
foreach(content IN ITEMS "gneiss_transform value;" "auto value = GNEISS_TRANSFORM_IDENTITY;")
  file(WRITE "${fixture}/engine/src/function/scene/scene_tree.hpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生场景树边界未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/scene/scene_tree.hpp" "")

foreach(name IN ITEMS scene_instance.hpp game_object.hpp instantiation.cpp scene_update.cpp)
  file(WRITE "${fixture}/engine/src/function/scene/${name}" "gneiss_transform value;\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生场景消费者未拒绝 C TRS：${name}")
  endif()
  file(WRITE "${fixture}/engine/src/function/scene/${name}" "")
endforeach()

file(MAKE_DIRECTORY "${fixture}/editor")
foreach(content IN ITEMS "gneiss_scene_instance_load();" "scene_instance scene_;" "gneiss_world_entity_create();")
  file(WRITE "${fixture}/editor/documents/scene_document.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "编辑会话边界未拒绝 ABI 回退：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/editor/documents/scene_document.cpp" "")

foreach(content IN ITEMS "gneiss::sdk::world owner;" "gneiss_type_registry_create();")
  file(WRITE "${fixture}/editor/documents/commands/author_property_edit.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "编辑模型边界未拒绝 SDK/Registry 回退：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/editor/documents/commands/author_property_edit.cpp" "")

foreach(content IN ITEMS "gneiss_property_value value;" "auto kind = GNEISS_PROPERTY_KIND_VEC3;")
  file(WRITE "${fixture}/editor/documents/commands/author_property_edit.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "原生属性边界未拒绝 C 布局：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/editor/documents/commands/author_property_edit.cpp" "")

file(WRITE "${fixture}/editor/viewport/editor_grid.hpp" "#include \"../engine/function/render/draw_types.hpp\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "Editor 父目录越界未被拒绝")
endif()
file(WRITE "${fixture}/editor/viewport/editor_grid.hpp" "")

# 输入服务只使用资产值与运行时输入状态，不接管加载后端。
file(MAKE_DIRECTORY "${fixture}/engine/src/function/input")
foreach(header IN ITEMS "caors/assets/asset_manager.hpp" "engine/platform/caors/adapter.hpp"
    "engine/resource/backend/caors/caors_action_map.hpp" "engine/resource/vfs/virtual_file_system.hpp"
    "engine/function/asset/asset_manager.hpp")
  file(WRITE "${fixture}/engine/src/function/input/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "输入服务边界未拒绝资源后端：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/input/probe.cpp" "#include <engine/resource/input/action_map.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "输入服务边界错误拒绝纯资源值")
endif()

file(MAKE_DIRECTORY "${fixture}/engine/src/resource")
foreach(header IN ITEMS "engine/core/meta/registry.hpp"
    "engine/core/meta/registration.hpp" "engine/core/serialization/json.hpp"
    "engine/core/serialization/serialization.hpp"
    "engine/function/scene/prefab_property_override.hpp" "engine/function/scene/world.hpp")
  file(WRITE "${fixture}/engine/src/resource/scene/scene_description.hpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "场景描述边界未拒绝运行时依赖：${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/resource/scene/scene_description.hpp" "#include <engine/resource/scene/prefab_property_data.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "场景描述边界错误拒绝作者值")
endif()

# 资产编排迁入功能层；底层资源不得反向依赖管理器。
file(WRITE "${fixture}/engine/src/resource/manager_probe.hpp" "#include <engine/function/asset/asset_manager.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
file(REMOVE "${fixture}/engine/src/resource/manager_probe.hpp")
if(status EQUAL 0)
  message(FATAL_ERROR "资源层未拒绝功能层资产管理器")
endif()

# 旧目录即使没有反向 include，也不得重新出现源码。
foreach(old_path IN ITEMS "engine/src/asset" "engine/src/function/resource" "engine/src/function/game")
  file(MAKE_DIRECTORY "${fixture}/${old_path}")
  file(WRITE "${fixture}/${old_path}/legacy.hpp" "// obsolete path\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(REMOVE "${fixture}/${old_path}/legacy.hpp")
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝旧目录：${old_path}")
  endif()
endforeach()

# Engine 各层禁止依赖 Application，应用组装可以正向依赖 Engine。
foreach(layer IN ITEMS core platform resource function/scene api)
  file(MAKE_DIRECTORY "${fixture}/engine/src/${layer}")
  file(WRITE "${fixture}/engine/src/${layer}/runtime_probe.hpp" "#include <application/application.hpp>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(REMOVE "${fixture}/engine/src/${layer}/runtime_probe.hpp")
  if(status EQUAL 0)
    message(FATAL_ERROR "未拒绝 Engine ${layer} 反向依赖 Application")
  endif()
endforeach()
foreach(header IN ITEMS editor/documents/scene_document.hpp editor/documents/scene_edit_session.hpp apps/editor/main.hpp host/runtime/application.hpp tooling/asset_build.hpp)
  file(WRITE "${fixture}/runtime/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "未拒绝 Application 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/runtime/probe.cpp" "#include <engine/function/asset/asset_manager.hpp>\n")
file(MAKE_DIRECTORY "${fixture}/runtime/api")
file(WRITE "${fixture}/runtime/api/probe.cpp" "extern \"C\" int allowed();\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "错误拒绝 Application 正向组装或 C ABI 适配")
endif()

# 业务命名空间按层统一；实现细节可用 detail。
foreach(content IN ITEMS "namespace gneiss::asset_internal {}" "namespace gneiss::engine {}" "namespace gneiss::engine::tasks {}" "namespace gneiss::runtime::game {}")
  file(WRITE "${fixture}/runtime/namespace_probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "命名空间检查未拒绝旧层级：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/runtime/namespace_probe.cpp" "namespace gneiss { class world; }\nnamespace gneiss::runtime {}\nnamespace gneiss::detail {}\nnamespace gneiss::tasks {}\nnamespace gneiss::tasks::detail {}\nnamespace gneiss::meta {}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "命名空间检查错误拒绝两层命名或 detail")
endif()

# 宿主是装配层；不能让业务层反向依赖它，也不能反向包含薄入口。
foreach(location IN ITEMS engine/src/core engine/src/platform engine/src/function/scene engine/src/resource)
  file(MAKE_DIRECTORY "${fixture}/${location}")
  file(WRITE "${fixture}/${location}/host_probe.hpp" "#include <host/common/protocol.hpp>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(REMOVE "${fixture}/${location}/host_probe.hpp")
  if(status EQUAL 0)
    message(FATAL_ERROR "未拒绝 ${location} 反向依赖 Host")
  endif()
endforeach()
# 资源类型与版本索引不能重新通过后端适配获取领域定义。
foreach(location IN ITEMS engine/src/resource/material/material_description.hpp engine/src/function/asset/resource_cache.hpp)
  foreach(header IN ITEMS engine/platform/caors/document_loader.hpp engine/resource/backend/caors/caors_material_document.hpp)
    file(WRITE "${fixture}/${location}" "#include <${header}>\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
      -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
      RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
    file(REMOVE "${fixture}/${location}")
    if(status EQUAL 0)
      message(FATAL_ERROR "未拒绝资源领域反向依赖：${location} -> ${header}")
    endif()
  endforeach()
endforeach()
file(WRITE "${fixture}/engine/src/resource/material/material_description.hpp" "#include <memory>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "错误拒绝内部资源类型使用标准共享所有权")
endif()

# 纹理 CPU 数据必须独立于具体容器格式。
foreach(header IN ITEMS engine/resource/texture/texture_ktx2.hpp engine/resource/texture/texture_decoder.hpp)
  file(WRITE "${fixture}/engine/src/resource/texture/texture_data.hpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(WRITE "${fixture}/engine/src/resource/texture/texture_data.hpp" "#include <vector>\n")
  if(status EQUAL 0)
    message(FATAL_ERROR "未拒绝纹理数据反向依赖：${header}")
  endif()
endforeach()

# 宿主只提交请求，不依赖渲染消费帧包。
file(MAKE_DIRECTORY "${fixture}/runtime")
file(WRITE "${fixture}/runtime/frame_probe.cpp"
  "#include <engine/function/render/detail/frame_packet.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝宿主包含私有帧包")
endif()
file(WRITE "${fixture}/runtime/frame_probe.cpp" "// 已清理帧包探针\n")

# Render 只开放服务接口，不开放 RID 表与引用关联实现。
foreach(header IN ITEMS "engine/function/render/detail/executor.hpp"
    "engine/function/render/detail/resource_storage.hpp"
    "engine/function/render/detail/service_impl.hpp"
    "engine/resource/texture/detail/texture_resource_access.hpp"
    "engine/resource/mesh/detail/mesh_resource_access.hpp"
    "engine/resource/material/detail/material_resource_access.hpp"
    "engine/resource/scene/detail/scene_asset_access.hpp")
  file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝私有渲染头 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/probe.cpp" "// 已清理私有头探针\n")

# Render 服务不得重新接管上层资源或销毁队列。
foreach(header IN ITEMS "engine/function/asset/detail/asset_construction.hpp"
    "engine/resource/resource_retirement_queue.hpp"
    "engine/resource/texture/detail/texture_resource_access.hpp")
  file(WRITE "${fixture}/engine/src/function/render/service_resources.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Render 反向包含 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/service_resources.cpp" "// 已清理反向依赖探针\n")

# Meta 默认注册表只能由 Meta 内部取得，业务侧使用无 registry 入口。
foreach(content IN ITEMS "auto value = meta::default_registry();" "using namespace meta; auto value = default_registry();")
  file(WRITE "${fixture}/editor/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝外部默认注册表访问")
  endif()
endforeach()
file(WRITE "${fixture}/editor/probe.cpp" "auto value = meta::type_of<int>();\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "边界检查错误拒绝无 registry 入口")
endif()
file(WRITE "${fixture}/editor/probe.cpp" "")

# 共享遍历必须独立于 JSON 后端。
file(MAKE_DIRECTORY "${fixture}/engine/src/core/serialization")
file(WRITE "${fixture}/engine/src/core/serialization/traversal.cpp" "#include <yyjson.h>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝共享遍历依赖 JSON")
endif()
file(WRITE "${fixture}/engine/src/core/serialization/traversal.cpp" "// 通用遍历\n")

# Value 编码与类型注册也不能反向依赖 JSON。
foreach(path IN ITEMS "serialization/policy.hpp" "meta/type_definition.hpp")
  file(WRITE "${fixture}/engine/src/core/${path}" "struct yyjson_val;\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 ${path} 依赖 JSON")
  endif()
  file(WRITE "${fixture}/engine/src/core/${path}" "// 已清理探针\n")
endforeach()

# 注册与内存访问仅使用通用扩展元数据，不可反向依赖序列化策略。
foreach(path IN ITEMS "registration.hpp" "registry.cpp" "type_definition.hpp" "instance.hpp"
    "builtin_registration.cpp" "math_registration.cpp" "views/sequence_view.hpp")
  file(WRITE "${fixture}/engine/src/core/meta/${path}" "#include <engine/core/serialization/policy.hpp>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Meta ${path} 依赖序列化")
  endif()
  file(WRITE "${fixture}/engine/src/core/meta/${path}" "// 已清理探针\n")
endforeach()

# 批次不得获得共享资产管理器的推进与关闭责任。
file(MAKE_DIRECTORY "${fixture}/engine/src/function/asset/detail")
foreach(operation IN ITEMS advance collect_unused stop request_stop)
  file(WRITE "${fixture}/engine/src/function/asset/detail/resource_load_group.hpp" "void probe() { service_.${operation}(); }\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(WRITE "${fixture}/engine/src/function/asset/detail/resource_load_group.hpp" "// 批次只观察请求\n")
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝资源批次调用 ${operation}")
  endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "清理后的批次边界正例未通过")
endif()

# 渲染前端桥接允许读取场景，但不能访问 GPU 后端或绕行 C TRS。
set(binding_probe "${fixture}/engine/src/function/scene/rendering/probe.cpp")
file(WRITE "${binding_probe}" "#include <engine/function/scene/world.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
file(WRITE "${binding_probe}" "")
if(NOT status EQUAL 0)
  message(FATAL_ERROR "场景渲染桥接读取 World 的正例未通过")
endif()
foreach(content IN ITEMS "#include <granit/device.hpp>" "#include <engine/function/render/backend/device.hpp>" "#include <engine/function/application/state.hpp>" "gneiss_transform value;")
  file(WRITE "${binding_probe}" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  file(WRITE "${binding_probe}" "")
  if(status EQUAL 0)
    message(FATAL_ERROR "渲染前端边界未拒绝 ${content}")
  endif()
endforeach()
file(WRITE "${fixture}/engine/src/function/render/backend/probe.cpp" "#include <engine/function/scene/world.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
file(WRITE "${fixture}/engine/src/function/render/backend/probe.cpp" "")
if(status EQUAL 0)
  message(FATAL_ERROR "渲染后端包含 World 未被拒绝")
endif()

# 当前 include/src 及新增模块逐项验证，并匹配具体诊断，防止因其他错误误判反例成功。
function(check_current_boundary path content expected)
  set(probe "${fixture}/${path}")
  file(WRITE "${probe}" "${content}
")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic ENCODING UTF-8)
  file(REMOVE "${probe}")
  string(REGEX REPLACE "[ \t\r\n]+" " " diagnostic "${diagnostic}")
  if(expected STREQUAL "")
    if(NOT status EQUAL 0)
      message(FATAL_ERROR "当前布局正例失败：${path}
${diagnostic}")
    endif()
  elseif(status EQUAL 0 OR NOT diagnostic MATCHES "${expected}")
    message(FATAL_ERROR "当前布局反例未命中预期规则：${path}
${diagnostic}")
  endif()
endfunction()
foreach(root IN ITEMS engine/include/gneiss/engine engine/src)
  foreach(header IN ITEMS gneiss/engine/core/serialization/policy.hpp core/serialization/policy.hpp)
    check_current_boundary("${root}/core/meta/current_probe.hpp"
      "#include <${header}>" "Meta 不得依赖 Serialization")
  endforeach()
  check_current_boundary("${root}/function/render/current_probe.hpp"
    "#include <gneiss/engine/function/scene/world.hpp>" "Render 不得反向包含")
  check_current_boundary("${root}/core/meta/current_probe.hpp"
    "#include <gneiss/engine/core/types/guid.hpp>" "")
endforeach()
foreach(root IN ITEMS editor runtime tooling/src project/src protocol/src)
  check_current_boundary("${root}/current_probe.cpp"
    "auto r = gneiss::meta::default_registry();" "不能直接取得默认表")
  check_current_boundary("${root}/current_probe.cpp"
    "#include <gneiss/engine/core/types/guid.hpp>" "")
endforeach()
check_current_boundary("engine/src/function/application/current_probe.cpp"
  "#include <gneiss/engine/function/asset/asset_manager.hpp>" "")
# 空树仍须失败，不能退化成扫描零文件也通过。
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}/empty"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE diagnostic ENCODING UTF-8)
if(status EQUAL 0 OR NOT diagnostic MATCHES "拒绝空检查")
  message(FATAL_ERROR "空源码树未被明确拒绝：${diagnostic}")
endif()
