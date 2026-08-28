# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

execute_process(
  COMMAND "${GNEISS_EDITOR}" --project "${MISSING_PROJECT}"
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE standard_output
  ERROR_VARIABLE standard_error
)
if(exit_code EQUAL 0)
  message(FATAL_ERROR "缺失工程启动应失败")
endif()
if(NOT standard_error MATCHES "阶段=工程加载.*路径=")
  message(FATAL_ERROR "启动诊断缺少阶段或路径：${standard_error}")
endif()
