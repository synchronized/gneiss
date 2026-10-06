# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
set(fixture "${GNEISS_BINARY_DIR}/api-boundary-fixture")
file(MAKE_DIRECTORY "${fixture}/src/engine/platform")
file(MAKE_DIRECTORY "${fixture}/src/editor")
file(WRITE "${fixture}/src/editor/probe.cpp" "")
file(MAKE_DIRECTORY "${fixture}/src/engine/api")
file(WRITE "${fixture}/src/engine/api/probe.cpp" "")
# 每次先恢复合法内容，避免上次中断的反例污染其他检查。
file(WRITE "${fixture}/src/engine/platform/probe.cpp" "#include <gneiss/core/result.hpp>\n")
file(MAKE_DIRECTORY "${fixture}/src/engine/core" "${fixture}/src/engine/function/world" "${fixture}/src/engine/function/application" "${fixture}/src/engine/core/reflection" "${fixture}/src/engine/function/render" "${fixture}/src/engine/asset")
# 清除上一次失败用例遗留的内容，保证反例可重复执行。
file(WRITE "${fixture}/src/engine/asset/probe.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/function/render/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/src/engine/asset/asset_preparation.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/asset/asset_preparation.hpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/asset/png_decoder.cpp" "#include <gneiss/core/result.h>\n")
file(WRITE "${fixture}/src/engine/core/reflection/probe.cpp" "#include <gneiss/reflection.h>\n")
file(WRITE "${fixture}/src/engine/function/world/probe.cpp" "#include <gneiss/render.h>\n")
file(WRITE "${fixture}/src/engine/function/application/probe.cpp" "#include <gneiss/application.h>\n")
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
  file(WRITE "${fixture}/src/engine/function/application/probe.cpp" "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "Application 边界检查未拒绝违规输入：${content}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/function/application/probe.cpp" "#include <gneiss/application.h>\n")

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
file(WRITE "${fixture}/src/engine/asset/png_decoder.cpp" "#include \"render/resource.hpp\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 PNG 解码依赖 Render")
endif()
file(WRITE "${fixture}/src/engine/asset/png_decoder.cpp" "#include <gneiss/core/result.h>\n")

foreach(layer IN ITEMS application world scene render asset apps)
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
    "engine/function/world/state.hpp" "asset/cache.hpp" "apps/editor/session.hpp"
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
  file(WRITE "${fixture}/src/engine/asset/probe.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 Asset 反向依赖 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/asset/probe.cpp" "#include <gneiss/core/result.h>\n")

foreach(header IN ITEMS "engine/function/render/render_asset_loader.hpp" "engine/function/render/render_resource_service.hpp"
    "engine/asset/resource_cache.hpp" "engine/core/rid_table.hpp")
  file(WRITE "${fixture}/src/engine/asset/asset_preparation.cpp" "#include <${header}>\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝 CPU 准备包含 ${header}")
  endif()
endforeach()
file(WRITE "${fixture}/src/engine/asset/asset_preparation.cpp" "#include <gneiss/core/result.h>\n")

file(WRITE "${fixture}/src/engine/asset/asset_preparation.hpp" "#include <engine/function/render/render_resource_data.hpp>\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝准备契约包含 Render 资源值")
endif()
file(WRITE "${fixture}/src/engine/asset/asset_preparation.hpp" "#include <gneiss/core/result.h>\n")

# 新路径反例：避免目录迁移后检查只匹配旧路径而静默失效。
foreach(header IN ITEMS "engine/function/world/state.hpp" "engine/function/scene/tree.hpp"
    "engine/function/application/state.hpp" "apps/editor/session.hpp" "editor/session.hpp"
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
foreach(header IN ITEMS "engine/function/application/state.hpp" "engine/function/render/service.hpp")
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

file(WRITE "${fixture}/src/engine/function/application/probe.cpp" "void create(const gneiss_application_desc&);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
  -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "边界检查未拒绝 Application 核心消费 ABI 描述")
endif()
file(WRITE "${fixture}/src/engine/function/application/probe.cpp" "")

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
  file(MAKE_DIRECTORY "${fixture}/src/engine/function/${module}")
  file(WRITE "${fixture}/src/engine/function/${module}/probe.cpp"
    "void log(const gneiss_log_message&);\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_SOURCE_DIR=${fixture}"
    -P "${GNEISS_SOURCE_DIR}/cmake/check_core_boundary.cmake"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "边界检查未拒绝内部 C 日志描述：${module}")
  endif()
  file(WRITE "${fixture}/src/engine/function/${module}/probe.cpp" "")
endforeach()
