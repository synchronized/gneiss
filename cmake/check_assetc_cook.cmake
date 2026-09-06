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

file(GLOB_RECURSE cooked_ktx2 "${cooked_root}/*.ktx2")
file(GLOB_RECURSE cooked_png "${cooked_root}/*.png")
if(NOT cooked_ktx2 OR cooked_png)
  message(FATAL_ERROR "Cook 输出必须包含 KTX2 且不得保留 PNG")
endif()

file(READ "${cooked_root}/textures/image-0.texture.json" texture_description)
if(NOT texture_description MATCHES "asset://textures/image-0\\.ktx2" OR
   texture_description MATCHES "\\.png")
  message(FATAL_ERROR "Texture 描述未重写为 KTX2 URI")
endif()
