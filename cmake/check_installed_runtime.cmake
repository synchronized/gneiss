# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_var IN ITEMS GNEISS_BUILD_DIR GNEISS_CONFIG)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少安装 Runtime 验收参数：${required_var}")
  endif()
endforeach()

set(install_dir "${GNEISS_BUILD_DIR}/runtime-prefix")
file(REMOVE_RECURSE "${install_dir}")
set(granit_build_dir "${GNEISS_BUILD_DIR}/_deps/gneiss_granit-build")
if(EXISTS "${granit_build_dir}/cmake_install.cmake")
  set(granit_install_command
      "${CMAKE_COMMAND}" --install "${granit_build_dir}" --prefix "${install_dir}"
  )
  if(NOT GNEISS_CONFIG STREQUAL "")
    list(APPEND granit_install_command --config "${GNEISS_CONFIG}")
  endif()
  execute_process(COMMAND ${granit_install_command} RESULT_VARIABLE granit_install_result)
  if(NOT granit_install_result EQUAL 0)
    message(FATAL_ERROR "Granit SDK 安装失败：${granit_install_result}")
  endif()
endif()
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
set(lantern_gallery "${install_dir}/share/gneiss/examples/lantern-gallery")
if(GNEISS_SHARED AND WIN32)
  set(lantern_module "${lantern_gallery}/modules/gneiss_lantern_gallery_game.dll")
elseif(GNEISS_SHARED AND APPLE)
  set(lantern_module "${lantern_gallery}/modules/libgneiss_lantern_gallery_game.dylib")
elseif(GNEISS_SHARED)
  set(lantern_module "${lantern_gallery}/modules/libgneiss_lantern_gallery_game.so")
endif()
if(NOT EXISTS "${runtime}" OR NOT EXISTS "${editor_demo}/gneiss.project.json" OR
   (GNEISS_SHARED AND
    (NOT EXISTS "${lantern_gallery}/gneiss.project.json" OR NOT EXISTS "${lantern_module}")))
  message(FATAL_ERROR "安装树缺少 Runtime、示例工程或游戏模块")
endif()

set(runtime_environment "${CMAKE_COMMAND}" -E env)
if(NOT WIN32)
  list(APPEND runtime_environment "LD_LIBRARY_PATH=${install_dir}/lib:$ENV{LD_LIBRARY_PATH}")
endif()
execute_process(
  COMMAND ${runtime_environment} "${runtime}" --smoke --project "${editor_demo}"
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_output
  ERROR_VARIABLE runtime_error
)
if(NOT runtime_result EQUAL 0 OR NOT runtime_output MATCHES "Runtime 已进入首帧" OR
   NOT runtime_output MATCHES "stage=shutdown")
  message(FATAL_ERROR
          "安装树 Runtime 启动失败：${runtime_result}\n${runtime_output}${runtime_error}")
endif()

if(GNEISS_SHARED)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GNEISS_SDK_ROOT=${install_dir}" "${CMAKE_COMMAND}"
            --preset game-debug-configure --fresh
    WORKING_DIRECTORY "${lantern_gallery}"
    RESULT_VARIABLE module_configure_result
  )
  if(NOT module_configure_result EQUAL 0)
    message(FATAL_ERROR "安装树 Lantern Gallery 模块配置失败：${module_configure_result}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build --preset game-debug --target gneiss_lantern_gallery_game
    WORKING_DIRECTORY "${lantern_gallery}"
    RESULT_VARIABLE module_build_result
  )
  if(NOT module_build_result EQUAL 0)
    message(FATAL_ERROR "安装树 Lantern Gallery 模块构建失败：${module_build_result}")
  endif()

  execute_process(
    COMMAND ${runtime_environment} "${runtime}" --smoke --project "${lantern_gallery}"
    RESULT_VARIABLE lantern_result
    OUTPUT_VARIABLE lantern_output
    ERROR_VARIABLE lantern_error
  )
  if(NOT lantern_result EQUAL 0 OR NOT lantern_output MATCHES "stage=game_module" OR
     NOT lantern_output MATCHES "stage=shutdown")
    message(FATAL_ERROR
            "安装树 Lantern Gallery 启动失败：${lantern_result}\n${lantern_output}${lantern_error}")
  endif()
endif()
