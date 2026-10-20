# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 每次使用新目录，保留日志便于排查；不删除既有工程或测试结果。
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(root "${TEST_PARENT}/runtime-async-input-${suffix}")
file(MAKE_DIRECTORY "${root}/modules" "${root}/assets/input")
file(COPY "${SOURCE_PROJECT}/assets/" DESTINATION "${root}/assets")
file(COPY "${INPUT_MAP}" DESTINATION "${root}/assets/input")
file(COPY "${GAME_MODULE}" DESTINATION "${root}/modules")
get_filename_component(module_name "${GAME_MODULE}" NAME_WE)
string(REGEX REPLACE "^lib" "" module_name "${module_name}")
file(READ "${SOURCE_PROJECT}/gneiss.project.json" project)
string(JSON project SET "${project}" version 2)
string(JSON project SET "${project}" input_map "\"asset://input/default.input-map.json\"")
string(JSON project SET "${project}" game_module
  "{\"name\":\"${module_name}\",\"directory\":\"modules\",\"configure_preset\":\"game-debug-configure\",\"build_preset\":\"game-debug\",\"build_target\":\"${module_name}\"}")
file(WRITE "${root}/gneiss.project.json" "${project}\n")
foreach(mode IN ITEMS asynchronous cooperative failure)
  set(extra)
  if(mode STREQUAL "cooperative")
    set(extra --cooperative-tasks)
  elseif(mode STREQUAL "failure")
    file(WRITE "${root}/assets/input/default.input-map.json" "{}")
  endif()
  execute_process(COMMAND "${GNEISS_RUNTIME}" --smoke --project "${root}" ${extra}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20 ENCODING UTF-8)
  file(WRITE "${root}/${mode}.log" "${output}\n${error}")
  if(mode STREQUAL "failure")
    if(result EQUAL 0 OR NOT error MATCHES "stage=input_map .*result=-" OR
       output MATCHES "stage=game_module")
      message(FATAL_ERROR "失败输入不应初始化游戏：${result}\n${output}\n${error}")
    endif()
  else()
    string(FIND "${output}" "stage=input_map " input_position)
    string(FIND "${output}" "stage=game_module " game_position)
    if(NOT result EQUAL 0 OR input_position LESS 0 OR game_position LESS input_position)
      message(FATAL_ERROR "输入必须先于游戏初始化：${result}\n${output}\n${error}")
    endif()
    if(mode STREQUAL "asynchronous" AND NOT output MATCHES "stage=input_map_pending")
      message(FATAL_ERROR "未走异步输入链路：${output}")
    endif()
    if(mode STREQUAL "cooperative" AND output MATCHES "stage=input_map_pending")
      message(FATAL_ERROR "cooperative 模式不应提交线程模式输入：${output}")
    endif()
  endif()
endforeach()

if(WIN32)
  # 恢复有效输入，验证 Editor 使用的正常停止文件协议；后台真实窗口不抢占桌面。
  file(COPY "${INPUT_MAP}" DESTINATION "${root}/assets/input")
  execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_LIST_DIR}/stop_protocol_test.ps1"
    -Runtime "${GNEISS_RUNTIME}" -Project "${root}" -TestRoot "${root}/stop"
    RESULT_VARIABLE stop_result OUTPUT_VARIABLE stop_output ERROR_VARIABLE stop_error TIMEOUT 25
    ENCODING UTF-8)
  if(NOT stop_result EQUAL 0)
    message(FATAL_ERROR "异步输入工程停止失败：${stop_output}\n${stop_error}")
  endif()
endif()
