# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required IN ITEMS GNEISS_ASSETC GNEISS_SOURCE GNEISS_OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "缺少 ${required}")
  endif()
endforeach()

set(author_root "${GNEISS_OUTPUT}/author")
set(cooked_root "${GNEISS_OUTPUT}/cooked")
set(cache_root "${GNEISS_OUTPUT}/cache")
file(REMOVE_RECURSE "${GNEISS_OUTPUT}")

execute_process(
  COMMAND "${GNEISS_ASSETC}" import "${GNEISS_SOURCE}" --output "${author_root}"
  RESULT_VARIABLE import_result
  OUTPUT_VARIABLE import_output
  ERROR_VARIABLE import_error
)
if(NOT import_result EQUAL 0)
  message(FATAL_ERROR "导入测试资产失败：\n${import_output}\n${import_error}")
endif()

execute_process(
  COMMAND
    "${GNEISS_ASSETC}" cook "${author_root}" --output "${cooked_root}" --cache
    "${cache_root}"
  RESULT_VARIABLE cook_result
  OUTPUT_VARIABLE cook_output
  ERROR_VARIABLE cook_error
)
if(NOT cook_result EQUAL 0)
  message(FATAL_ERROR "Cook 测试资产失败：\n${cook_output}\n${cook_error}")
endif()

file(GLOB_RECURSE cooked_texture "${cooked_root}/*.gneiss-texture")
file(GLOB_RECURSE cooked_png "${cooked_root}/*.png")
if(NOT cooked_texture OR cooked_png)
  message(FATAL_ERROR "Cook 输出必须包含 Gneiss 运行纹理且不得保留 PNG")
endif()

foreach(texture IN LISTS cooked_texture)
  foreach(command IN ITEMS inspect validate)
    execute_process(COMMAND "${GNEISS_ASSETC}" "${command}" "${texture}"
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "运行纹理 ${command} 失败：${output}\n${error}")
    endif()
    if(command STREQUAL "inspect" AND
       (NOT output MATCHES "Format=BC7_" OR NOT output MATCHES "Format=RGBA8_" OR
        NOT output MATCHES "SHA256=ok" OR NOT output MATCHES "RowBytes="))
      message(FATAL_ERROR "运行纹理检查未输出双变体与 Mip 布局：${output}")
    endif()
  endforeach()
endforeach()
file(WRITE "${GNEISS_OUTPUT}/invalid.gneiss-texture" "invalid")
foreach(command IN ITEMS inspect validate)
  execute_process(COMMAND "${GNEISS_ASSETC}" "${command}" "${GNEISS_OUTPUT}/invalid.gneiss-texture"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 1 OR error STREQUAL "")
    message(FATAL_ERROR "损坏运行纹理必须返回诊断及退出码 1：${result} ${output} ${error}")
  endif()
endforeach()
file(READ "${cooked_root}/textures/image-0.texture.json" texture_description)
if(NOT texture_description MATCHES "asset://textures/image-0\\.gneiss-texture" OR
   texture_description MATCHES "\\.png")
  message(FATAL_ERROR "Texture 描述未重写为 Gneiss 运行纹理 URI")
endif()
