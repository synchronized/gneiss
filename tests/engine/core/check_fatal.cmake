# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
execute_process(COMMAND "${PROGRAM}" "${MODE}" RESULT_VARIABLE result ERROR_VARIABLE diagnostic TIMEOUT 10)
if(NOT result MATCHES "^[0-9]+$" OR result EQUAL 0)
  message(FATAL_ERROR "致命路径未正常终止子进程：${result} ${diagnostic}")
endif()
# 某些运行库在 noexcept 违约进入 terminate 时不保留 current_exception。
# 此时只能使用通用致命诊断，但仍须非零退出，不能吞掉分配失败。
if(MODE STREQUAL "allocation-boundary")
  if(NOT diagnostic MATCHES "Gneiss fatal: (out of memory|unhandled exception)")
    message(FATAL_ERROR "缺少分配边界致命诊断：${diagnostic}")
  endif()
  return()
endif()
if(MODE STREQUAL "oom" OR MODE STREQUAL "direct" OR MODE STREQUAL "worker-oom")
  set(expected "Gneiss fatal: out of memory")
else()
  set(expected "Gneiss fatal: unhandled exception")
endif()
string(FIND "${diagnostic}" "${expected}" found)
if(found LESS 0)
  message(FATAL_ERROR "缺少预期致命诊断：${diagnostic}")
endif()
