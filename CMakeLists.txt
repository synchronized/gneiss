# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(
  gneiss
  VERSION 0.52.0
  DESCRIPTION "基于 Granit 构建的现代 C++20 游戏引擎"
  LANGUAGES C CXX
)

# 依赖模块也会声明安装规则，必须在加载它们之前初始化安装目录。
include(GNUInstallDirs)

if(PROJECT_IS_TOP_LEVEL)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

option(GNEISS_TASK_THREADS "启用任务工作线程后端" ON)
option(BUILD_SHARED_LIBS "构建共享库" ON)
option(GNEISS_BUILD_TESTING "构建 Gneiss 测试" ON)
option(GNEISS_BUILD_EXAMPLES "构建 Gneiss 示例" ${PROJECT_IS_TOP_LEVEL})
option(GNEISS_BUILD_TOOLS "构建 Gneiss 离线工具" ${PROJECT_IS_TOP_LEVEL})
option(GNEISS_BUILD_EDITOR "构建 Gneiss Editor" OFF)
option(GNEISS_BUILD_RUNTIME "构建 Gneiss Runtime 宿主" OFF)

list(APPEND CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies"
)
include(gneiss_utility)
include(profiling)
include(granit)
include(libassert)
gneiss_resolve_granit_runtime()
if(GNEISS_BUILD_TOOLS OR GNEISS_BUILD_TESTING)
  gneiss_resolve_granit_tools()
endif()
include(CTest)

add_subdirectory(3rd)
if(GNEISS_BUILD_EDITOR OR GNEISS_BUILD_RUNTIME OR (GNEISS_TASK_THREADS AND NOT EMSCRIPTEN))
  include(libuv)
  gneiss_resolve_libuv()
endif()
include(glm)
add_subdirectory(engine)
if(GNEISS_BUILD_TOOLS)
  include(bc7enc)
  gneiss_resolve_bc7enc()
  include(fastgltf)
  gneiss_resolve_fastgltf()
  include(mikktspace)
  gneiss_resolve_mikktspace()
  add_subdirectory(tooling)
endif()
if(GNEISS_BUILD_EDITOR OR GNEISS_BUILD_RUNTIME)
  add_subdirectory(project)
  add_subdirectory(protocol)
endif()
if(GNEISS_BUILD_RUNTIME)
  add_subdirectory(runtime)
endif()
if(GNEISS_BUILD_EDITOR)
  include(editor_fonts)
  gneiss_resolve_editor_fonts()
  include(imgui)
  gneiss_resolve_imgui()
  include(imguizmo)
  gneiss_resolve_imguizmo()
  add_subdirectory(editor)
endif()

include("${PROJECT_SOURCE_DIR}/cmake/package/install.cmake")

if(GNEISS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(GNEISS_BUILD_TESTING AND BUILD_TESTING)
  add_subdirectory(tests)
endif()
