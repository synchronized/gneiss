# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

if(NOT DEFINED GNEISS_ASSETC OR NOT DEFINED GNEISS_LANTERN_SOURCE OR
   NOT DEFINED GNEISS_LANTERN_OUTPUT)
  message(FATAL_ERROR "Lantern 导入检查缺少必要路径")
endif()

file(REMOVE_RECURSE "${GNEISS_LANTERN_OUTPUT}")
execute_process(
  COMMAND "${GNEISS_ASSETC}" import "${GNEISS_LANTERN_SOURCE}" --output
          "${GNEISS_LANTERN_OUTPUT}"
  RESULT_VARIABLE import_result
  OUTPUT_VARIABLE import_output
  ERROR_VARIABLE import_error
)
if(NOT import_result EQUAL 0)
  message(FATAL_ERROR "Lantern 导入失败：${import_output}${import_error}")
endif()

set(base_color "${GNEISS_LANTERN_OUTPUT}/textures/image-0.png")
set(scene "${GNEISS_LANTERN_OUTPUT}/scenes/scene.scene.json")
if(NOT EXISTS "${base_color}" OR NOT EXISTS "${scene}")
  message(FATAL_ERROR "Lantern 导入缺少基础颜色纹理或场景")
endif()
file(SIZE "${base_color}" base_color_size)
if(base_color_size LESS 1)
  message(FATAL_ERROR "Lantern 嵌入基础颜色纹理为空")
endif()
file(REMOVE_RECURSE "${GNEISS_LANTERN_OUTPUT}")
