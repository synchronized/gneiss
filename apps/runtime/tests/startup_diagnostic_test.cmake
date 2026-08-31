# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

execute_process(
  COMMAND "${GNEISS_RUNTIME}"
  RESULT_VARIABLE argument_exit_code
  OUTPUT_VARIABLE argument_output
  ERROR_VARIABLE argument_error
)
if(NOT argument_exit_code EQUAL 64)
  message(FATAL_ERROR "Runtime 无参数启动应返回 64，实际为 ${argument_exit_code}")
endif()
if(NOT argument_error MATCHES "level=ERROR.*stage=arguments.*result=-2")
  message(FATAL_ERROR "Runtime 参数诊断字段不完整：${argument_error}")
endif()

execute_process(
  COMMAND "${GNEISS_RUNTIME}" --project "${MISSING_PROJECT}"
  RESULT_VARIABLE project_exit_code
  OUTPUT_VARIABLE project_output
  ERROR_VARIABLE project_error
)
if(NOT project_exit_code EQUAL 2)
  message(FATAL_ERROR "Runtime 缺失工程启动应返回 2，实际为 ${project_exit_code}")
endif()
if(NOT project_error MATCHES "level=ERROR.*stage=project_root.*context=.*missing-runtime-project")
  message(FATAL_ERROR "Runtime 工程诊断字段不完整：${project_error}")
endif()
