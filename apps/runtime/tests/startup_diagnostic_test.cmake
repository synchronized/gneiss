# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

file(REMOVE_RECURSE "${RUNTIME_TEST_DIR}")
file(MAKE_DIRECTORY "${RUNTIME_TEST_DIR}")
set(runtime_log "${RUNTIME_TEST_DIR}/runtime.log")

execute_process(
  COMMAND "${GNEISS_RUNTIME}" --log-file "${runtime_log}"
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
  COMMAND "${GNEISS_RUNTIME}" --project "${MISSING_PROJECT}" --log-file "${runtime_log}"
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

set(missing_scene_project "${RUNTIME_TEST_DIR}/missing-scene-project")
file(MAKE_DIRECTORY "${missing_scene_project}/assets")
file(
  WRITE "${missing_scene_project}/gneiss.project.json"
  [=[{
  "format": "gneiss.project",
  "version": 1,
  "name": "Missing Scene",
  "asset_root": "assets",
  "startup_scene": "asset://scenes/missing.scene.json"
}
]=]
)
execute_process(
  COMMAND "${GNEISS_RUNTIME}" --project "${missing_scene_project}" --log-file "${runtime_log}"
  RESULT_VARIABLE scene_exit_code
  OUTPUT_VARIABLE scene_output
  ERROR_VARIABLE scene_error
)
if(NOT scene_exit_code EQUAL 2 OR NOT scene_error MATCHES "stage=startup_scene")
  message(FATAL_ERROR "Runtime 缺失启动场景诊断不正确：${scene_error}")
endif()

if(NOT EXISTS "${runtime_log}")
  message(FATAL_ERROR "Runtime 未创建指定的日志文件")
endif()
file(READ "${runtime_log}" runtime_log_content)
if(NOT runtime_log_content MATCHES "stage=arguments" OR
   NOT runtime_log_content MATCHES "stage=project_root" OR
   NOT runtime_log_content MATCHES "stage=startup_scene")
  message(FATAL_ERROR "Runtime 日志文件缺少启动诊断：${runtime_log_content}")
endif()

set(blocking_file "${RUNTIME_TEST_DIR}/not-a-directory")
file(WRITE "${blocking_file}" "阻止创建日志目录")
execute_process(
  COMMAND "${GNEISS_RUNTIME}" --project "${MISSING_PROJECT}" --log-file
          "${blocking_file}/runtime.log"
  RESULT_VARIABLE log_failure_exit_code
  OUTPUT_VARIABLE log_failure_output
  ERROR_VARIABLE log_failure_error
)
if(NOT log_failure_exit_code EQUAL 2)
  message(FATAL_ERROR "日志不可写不应覆盖工程错误，实际返回 ${log_failure_exit_code}")
endif()
if(NOT log_failure_error MATCHES "stage=log_file" OR
   NOT log_failure_error MATCHES "stage=project_root")
  message(FATAL_ERROR "日志不可写时缺少警告或原始诊断：${log_failure_error}")
endif()

string(REPEAT "x" 1048576 oversized_log)
file(WRITE "${runtime_log}" "${oversized_log}")
execute_process(
  COMMAND "${GNEISS_RUNTIME}" --project "${MISSING_PROJECT}" --log-file "${runtime_log}"
  RESULT_VARIABLE rotation_exit_code
  OUTPUT_QUIET
  ERROR_QUIET
)
if(NOT rotation_exit_code EQUAL 2 OR NOT EXISTS "${runtime_log}.1")
  message(FATAL_ERROR "Runtime 日志达到 1 MiB 后未完成单文件轮转")
endif()
