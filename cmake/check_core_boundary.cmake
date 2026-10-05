# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED GNEISS_SOURCE_DIR)
  message(FATAL_ERROR "缺少 GNEISS_SOURCE_DIR")
endif()
# 所有内部模块适用；公共边界单独位于 api/c。
file(GLOB_RECURSE sources "${GNEISS_SOURCE_DIR}/src/*.cpp"
                          "${GNEISS_SOURCE_DIR}/src/*.h"
                          "${GNEISS_SOURCE_DIR}/src/*.hpp")
list(FILTER sources EXCLUDE REGEX "/src/api/c/")
if(NOT sources)
  message(FATAL_ERROR "内部模块 检查未找到源码，拒绝空检查")
endif()
foreach(source IN LISTS sources)
  file(READ "${source}" content)
  if(source MATCHES "(/src/engine/asset/asset_(preparation|parsing)|/src/render/(render_asset_preparation|render_resource_data))\\.(cpp|hpp)$" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](render/(render_asset_loader|render_resource_service)\\.(h|hpp)|engine/(asset/resource_cache|core/rid_table)\\.hpp)[>\"]")
    message(FATAL_ERROR "CPU 准备与值类型不得包含资源发布、缓存或 RID 表：${source}")
  endif()
  if(source MATCHES "/src/engine/core/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/)?(platform|function|application|world|scene|render|asset|editor|tooling|apps)/|gneiss/application\\.h[>\"])")
    message(FATAL_ERROR "Core 不得依赖平台或上层实现及 Application 协议：${source}")
  endif()
  if(source MATCHES "/src/engine/platform/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](application|world|scene|render|asset|apps)/")
    message(FATAL_ERROR "Platform 不得反向包含上层实现：${source}")
  endif()
  if(source MATCHES "/src/render/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"](world|scene|application)/")
    message(FATAL_ERROR "Render 不得反向包含 World / Scene / Application：${source}")
  endif()
  if(source MATCHES "/src/engine/asset/" AND
      content MATCHES "#[ \t]*include[ \t]*[<\"]((engine/)?(function|render|world|scene|application|editor|tooling|apps)/)")
    message(FATAL_ERROR "Asset 不得反向包含功能层或宿主实现：${source}")
  endif()
  if(content MATCHES "extern[ \t]+\"C\"")
    message(FATAL_ERROR "内部模块 不得定义 C ABI 入口：${source}")
  endif()
  if(content MATCHES "#[ \t]*include[ \t]*[<\"]gneiss/(application|world|scene|asset|render|reflection|game_module|gneiss)\\.hpp[>\"]")
    message(FATAL_ERROR "内部模块 不得依赖公共 SDK 包装：${source}")
  endif()
  if(content MATCHES "gneiss_(world|scene_node|application|type_registry|game_context|asset_uri)_[a-z_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回 公共 C ABI：${source}")
  endif()
  if(content MATCHES "gneiss_(transform|camera)_type_id[ \t\r\n]*\\(")
    message(FATAL_ERROR "内部模块不得绕回公共类型 ID 入口：${source}")
  endif()
endforeach()
