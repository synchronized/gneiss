# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/modules")
file(COPY "${SOURCE_PROJECT}/assets" DESTINATION "${TEST_ROOT}")
file(COPY "${GAME_MODULE}" DESTINATION "${TEST_ROOT}/modules")
get_filename_component(module_filename "${GAME_MODULE}" NAME)
if(WIN32)
  string(REGEX REPLACE "\\.dll$" "" module_name "${module_filename}")
elseif(APPLE)
  string(REGEX REPLACE "^lib|\\.dylib$" "" module_name "${module_filename}")
else()
  string(REGEX REPLACE "^lib|\\.so$" "" module_name "${module_filename}")
endif()
file(
  WRITE "${TEST_ROOT}/gneiss.project.json"
  "{\n"
  "  \"format\": \"gneiss.project\",\n"
  "  \"version\": 2,\n"
  "  \"name\": \"Runtime Game Module Test\",\n"
  "  \"asset_root\": \"assets\",\n"
  "  \"startup_scene\": \"asset://scenes/main.scene.json\",\n"
  "  \"game_module\": {\n"
  "    \"name\": \"${module_name}\",\n"
  "    \"directory\": \"modules\",\n"
  "    \"build_preset\": \"game-debug\",\n"
  "    \"build_target\": \"${module_name}\"\n"
  "  }\n"
  "}\n"
)
execute_process(
  COMMAND "${GNEISS_RUNTIME}" --smoke --project "${TEST_ROOT}"
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_output
  ERROR_VARIABLE runtime_error
)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "带游戏模块的 Runtime 失败：${runtime_result}\n${runtime_output}\n${runtime_error}")
endif()
if(NOT runtime_output MATCHES "stage=game_module" OR
   NOT runtime_output MATCHES "gneiss.test.fixture")
  message(FATAL_ERROR "Runtime 未记录游戏模块初始化：${runtime_output}")
endif()
