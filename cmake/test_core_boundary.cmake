# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
set(fixture "${GNEISS_BINARY_DIR}/function-boundary-fixture")
file(MAKE_DIRECTORY "${fixture}/src/engine/platform")
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
foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>")
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

foreach(content IN ITEMS "extern \"C\" int forbidden();" "#include <gneiss/application.hpp>"
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
    "gneiss/application.h")
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
