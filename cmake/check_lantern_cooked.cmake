# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required IN ITEMS GNEISS_ASSETC GNEISS_RUNTIME GNEISS_CORRUPT GNEISS_PROJECT GNEISS_OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "缺少 ${required}")
  endif()
endforeach()

# 测试重复运行时清理专属输出；仅允许删除构建树中约定的测试目录。
cmake_path(SET expected_output NORMALIZE "${GNEISS_PROJECT}/../../lantern-cooked-test")
cmake_path(SET actual_output NORMALIZE "${GNEISS_OUTPUT}")
if(NOT IS_ABSOLUTE "${actual_output}" OR NOT actual_output STREQUAL expected_output OR
   IS_SYMLINK "${actual_output}")
  message(FATAL_ERROR "拒绝清理约定构建目录以外的路径：${actual_output}")
endif()
file(REMOVE_RECURSE "${actual_output}")
file(MAKE_DIRECTORY "${GNEISS_OUTPUT}")
file(COPY "${GNEISS_PROJECT}/gneiss.project.json" "${GNEISS_PROJECT}/modules"
     DESTINATION "${GNEISS_OUTPUT}")
execute_process(COMMAND "${GNEISS_ASSETC}" cook "${GNEISS_PROJECT}/assets"
  --output "${GNEISS_OUTPUT}/assets" --cache "${GNEISS_OUTPUT}/cache"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Lantern Cook 失败：${output}\n${error}")
endif()

file(GLOB_RECURSE textures "${GNEISS_OUTPUT}/assets/*.gneiss-texture")
file(GLOB_RECURSE pngs "${GNEISS_OUTPUT}/assets/*.png")
if(NOT textures OR pngs)
  message(FATAL_ERROR "Lantern 必须生成运行纹理且不得保留作者 PNG")
endif()
foreach(texture IN LISTS textures)
  execute_process(COMMAND "${GNEISS_ASSETC}" inspect "${texture}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0 OR NOT output MATCHES "Format=BC7_" OR
     NOT output MATCHES "Format=RGBA8_")
    message(FATAL_ERROR "Lantern 纹理检查失败：${output}\n${error}")
  endif()
endforeach()
# 每次使用新日志，避免上次成功记录掩盖本次未上传。
set(log "${GNEISS_OUTPUT}/texture-upload.log")
file(WRITE "${log}" "")
execute_process(COMMAND "${GNEISS_RUNTIME}" --smoke --project "${GNEISS_OUTPUT}"
  --log-file "${log}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  TIMEOUT 30)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Cooked Lantern 运行失败：${output}\n${error}")
endif()
file(READ "${log}" events)
if(NOT events MATCHES "stage=ready variant=[0-9]+ format=(BC7|RGBA8)_(SRGB|UNORM) mips=[1-9][0-9]* bytes=[1-9][0-9]*" OR
   events MATCHES "level=(ERROR|FATAL)")
  message(FATAL_ERROR "缺少纹理上传成功记录或发生错误：\n${events}")
endif()

# image-0 是灯笼材质实际引用的基础颜色纹理；破坏所有变体可同时覆盖 BC7 与 RGBA8 设备。
set(texture "${GNEISS_OUTPUT}/assets/textures/image-0.gneiss-texture")
set(backup "${GNEISS_OUTPUT}/texture-backup.bin")
file(COPY_FILE "${texture}" "${backup}")
execute_process(COMMAND "${GNEISS_CORRUPT}" "${texture}" RESULT_VARIABLE damaged)
if(NOT damaged EQUAL 0)
  file(COPY_FILE "${backup}" "${texture}")
  message(FATAL_ERROR "创建损坏纹理夹具失败")
endif()
set(failure_log "${GNEISS_OUTPUT}/texture-upload-failure.log")
file(WRITE "${failure_log}" "")
execute_process(COMMAND "${GNEISS_RUNTIME}" --smoke --project "${GNEISS_OUTPUT}"
  --log-file "${failure_log}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  TIMEOUT 30)
file(COPY_FILE "${backup}" "${texture}")
file(READ "${failure_log}" events)
if(NOT result EQUAL 7 OR NOT events MATCHES "stage=write variant=" OR
   NOT events MATCHES "level=ERROR" OR events MATCHES "stage=ready")
  message(FATAL_ERROR "损坏负载必须报告上传失败并返回运行错误（7）：${result}\n${events}\n${error}")
endif()
