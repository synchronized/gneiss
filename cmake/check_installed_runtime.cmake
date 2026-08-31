# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_var IN ITEMS GNEISS_BUILD_DIR GNEISS_CONFIG)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少安装 Runtime 验收参数：${required_var}")
  endif()
endforeach()

set(install_dir "${GNEISS_BUILD_DIR}/runtime-prefix")
file(REMOVE_RECURSE "${install_dir}")
set(install_command "${CMAKE_COMMAND}" --install "${GNEISS_BUILD_DIR}" --prefix "${install_dir}")
if(NOT GNEISS_CONFIG STREQUAL "")
  list(APPEND install_command --config "${GNEISS_CONFIG}")
endif()
execute_process(COMMAND ${install_command} RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Gneiss Runtime 安装失败：${install_result}")
endif()

if(WIN32)
  set(runtime "${install_dir}/bin/gneiss_runtime.exe")
else()
  set(runtime "${install_dir}/bin/gneiss_runtime")
endif()
set(editor_demo "${install_dir}/share/gneiss/projects/editor-demo")
if(NOT EXISTS "${runtime}" OR NOT EXISTS "${editor_demo}/gneiss.project.json")
  message(FATAL_ERROR "安装树缺少 Runtime 或 Editor Demo 工程")
endif()

set(runtime_command "${CMAKE_COMMAND}" -E env)
if(NOT WIN32)
  list(APPEND runtime_command "LD_LIBRARY_PATH=${install_dir}/lib:$ENV{LD_LIBRARY_PATH}")
endif()
list(APPEND runtime_command "${runtime}" --smoke --project "${editor_demo}")
execute_process(
  COMMAND ${runtime_command}
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_output
  ERROR_VARIABLE runtime_error
)
if(NOT runtime_result EQUAL 0 OR NOT runtime_output MATCHES "stage=first_frame" OR
   NOT runtime_output MATCHES "stage=shutdown")
  message(FATAL_ERROR
          "安装树 Runtime 启动失败：${runtime_result}\n${runtime_output}${runtime_error}")
endif()
