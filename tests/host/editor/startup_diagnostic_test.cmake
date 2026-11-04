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
string(FIND "${standard_error}" "-11" result_position)
string(FIND "${standard_error}" "${MISSING_PROJECT}" path_position)
if(result_position EQUAL -1 OR path_position EQUAL -1)
  message(FATAL_ERROR "启动诊断缺少阶段或路径：${standard_error}")
endif()
