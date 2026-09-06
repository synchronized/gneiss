# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_var IN ITEMS GNEISS_BUILD_DIR GNEISS_CONFIG)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少安装 Runtime 验收参数：${required_var}")
  endif()
endforeach()

set(install_dir "${GNEISS_BUILD_DIR}/runtime-prefix")
set(template_source_dir "${GNEISS_BUILD_DIR}/runtime-template-source")
set(package_dir "${GNEISS_BUILD_DIR}/runtime-package")
set(package_repeat_dir "${GNEISS_BUILD_DIR}/runtime-package-repeat")
set(shipping_package_dir "${GNEISS_BUILD_DIR}/runtime-shipping-package")
file(REMOVE_RECURSE "${install_dir}" "${template_source_dir}" "${package_dir}"
     "${package_dir}.zip" "${package_repeat_dir}" "${package_repeat_dir}.zip"
     "${shipping_package_dir}")
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
  set(project_tool "${install_dir}/bin/gneiss_project.exe")
else()
  set(runtime "${install_dir}/bin/gneiss_runtime")
  set(project_tool "${install_dir}/bin/gneiss_project")
endif()
set(editor_demo "${install_dir}/share/gneiss/projects/editor-demo")
set(lantern_gallery "${install_dir}/share/gneiss/examples/lantern-gallery")
set(game_template "${install_dir}/share/gneiss/templates/game")
if(GNEISS_SHARED AND WIN32)
  set(lantern_module "${lantern_gallery}/modules/gneiss_lantern_gallery_game.dll")
elseif(GNEISS_SHARED AND APPLE)
  set(lantern_module "${lantern_gallery}/modules/libgneiss_lantern_gallery_game.dylib")
elseif(GNEISS_SHARED)
  set(lantern_module "${lantern_gallery}/modules/libgneiss_lantern_gallery_game.so")
endif()
if(NOT EXISTS "${runtime}" OR NOT EXISTS "${project_tool}" OR
   NOT EXISTS "${editor_demo}/gneiss.project.json" OR
   NOT EXISTS "${game_template}/gneiss.project.json" OR
   NOT EXISTS "${game_template}/CMakeLists.txt" OR
   NOT EXISTS "${game_template}/game_module.cpp" OR
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
if(NOT runtime_result EQUAL 0 OR NOT runtime_output MATCHES "@gneiss-log-v1" OR
   NOT runtime_output MATCHES "\"source\":\"application\"" OR
   NOT runtime_output MATCHES "stage=shutdown")
  message(FATAL_ERROR
          "安装树 Runtime 启动失败：${runtime_result}\n${runtime_output}${runtime_error}")
endif()

if(GNEISS_SHARED)
  execute_process(
    COMMAND ${runtime_environment} "${project_tool}" create "${template_source_dir}"
            "Installed Workflow"
            "${game_template}"
    RESULT_VARIABLE template_create_result
  )
  if(NOT template_create_result EQUAL 0)
    message(FATAL_ERROR "安装树游戏工程创建失败：${template_create_result}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GNEISS_SDK_ROOT=${install_dir}" "${CMAKE_COMMAND}"
            --preset game-debug-configure --fresh
    WORKING_DIRECTORY "${template_source_dir}"
    RESULT_VARIABLE template_configure_result
  )
  if(NOT template_configure_result EQUAL 0)
    message(FATAL_ERROR "安装树游戏模板配置失败：${template_configure_result}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build --preset game-debug --target gneiss_game
    WORKING_DIRECTORY "${template_source_dir}"
    RESULT_VARIABLE template_build_result
  )
  if(NOT template_build_result EQUAL 0)
    message(FATAL_ERROR "安装树游戏模板构建失败：${template_build_result}")
  endif()
  execute_process(
    COMMAND ${runtime_environment} "${runtime}" --smoke --project "${template_source_dir}"
    RESULT_VARIABLE template_runtime_result
    OUTPUT_VARIABLE template_runtime_output
    ERROR_VARIABLE template_runtime_error
  )
  if(NOT template_runtime_result EQUAL 0 OR
     NOT template_runtime_output MATCHES "gneiss.game.installed.workflow" OR
     NOT template_runtime_output MATCHES "stage=shutdown")
    message(FATAL_ERROR
            "安装树游戏模板启动失败：${template_runtime_result}\n${template_runtime_output}${template_runtime_error}"
    )
  endif()

  execute_process(
    COMMAND ${runtime_environment} "${project_tool}" package "${template_source_dir}" "${runtime}"
            "${package_dir}" development --zip
    RESULT_VARIABLE package_export_result
    OUTPUT_VARIABLE package_export_output
    ERROR_VARIABLE package_export_error
  )
  if(NOT package_export_result EQUAL 0 OR EXISTS "${package_dir}/CMakeLists.txt" OR
     EXISTS "${package_dir}/sources" OR NOT EXISTS "${package_dir}/gneiss.project.json" OR
     NOT EXISTS "${package_dir}/gneiss.package.json" OR NOT EXISTS "${package_dir}.zip" OR
     NOT EXISTS "${package_dir}/assets/.gneiss-build.json" OR
     NOT EXISTS "${package_dir}/assets/debug/unused.json" OR
     NOT package_export_output MATCHES "资产构建：源=" OR
     NOT package_export_output MATCHES "资产 \\[1/")
    message(FATAL_ERROR
            "Development 发布包导出失败：${package_export_result}\n${package_export_output}${package_export_error}")
  endif()
  execute_process(
    COMMAND ${runtime_environment} "${project_tool}" verify "${package_dir}"
    RESULT_VARIABLE package_verify_result
  )
  if(NOT package_verify_result EQUAL 0)
    message(FATAL_ERROR "Development 发布包清单校验失败：${package_verify_result}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${package_dir}.zip"
    RESULT_VARIABLE package_archive_result
    OUTPUT_VARIABLE package_archive_output
  )
  if(NOT package_archive_result EQUAL 0 OR
     NOT package_archive_output MATCHES "gneiss.package.json")
    message(FATAL_ERROR "Development ZIP 无法读取：${package_archive_result}")
  endif()
  execute_process(
    COMMAND ${runtime_environment} "${project_tool}" export "${template_source_dir}" "${runtime}"
            "${package_repeat_dir}" development --zip
    RESULT_VARIABLE package_repeat_result
    OUTPUT_VARIABLE package_repeat_output
  )
  if(NOT package_repeat_result EQUAL 0 OR NOT package_repeat_output MATCHES "缓存命中")
    message(FATAL_ERROR "重复发布包导出失败：${package_repeat_result}")
  endif()
  file(SHA256 "${package_dir}.zip" package_archive_hash)
  file(SHA256 "${package_repeat_dir}.zip" package_repeat_hash)
  if(NOT package_archive_hash STREQUAL package_repeat_hash)
    message(FATAL_ERROR "相同输入生成的 ZIP 不一致")
  endif()
  if(WIN32)
    execute_process(
      COMMAND cmd /c "${package_dir}/run.cmd" --smoke
      RESULT_VARIABLE package_runtime_result
      OUTPUT_VARIABLE package_runtime_output
      ERROR_VARIABLE package_runtime_error
    )
  else()
    execute_process(
      COMMAND sh "${package_dir}/run.sh" --smoke
      RESULT_VARIABLE package_runtime_result
      OUTPUT_VARIABLE package_runtime_output
      ERROR_VARIABLE package_runtime_error
    )
  endif()
  if(NOT package_runtime_result EQUAL 0 OR
     NOT package_runtime_output MATCHES "gneiss.game.installed.workflow" OR
     NOT package_runtime_output MATCHES "stage=shutdown")
    message(FATAL_ERROR
            "可运行目录包启动失败：${package_runtime_result}\n${package_runtime_output}${package_runtime_error}"
    )
  endif()

  execute_process(
    COMMAND ${runtime_environment} "${project_tool}" package "${template_source_dir}" "${runtime}"
            "${shipping_package_dir}" shipping
    RESULT_VARIABLE shipping_export_result
  )
  file(GLOB_RECURSE shipping_symbols "${shipping_package_dir}/*.pdb")
  if(NOT shipping_export_result EQUAL 0 OR
     NOT EXISTS "${shipping_package_dir}/gneiss.package.json" OR
     EXISTS "${shipping_package_dir}/sources" OR
     EXISTS "${shipping_package_dir}/assets/.gneiss-build.json" OR
     EXISTS "${shipping_package_dir}/assets/debug/unused.json" OR shipping_symbols)
    message(FATAL_ERROR "Shipping 发布包导出失败：${shipping_export_result}")
  endif()
  if(WIN32)
    execute_process(
      COMMAND cmd /c "${shipping_package_dir}/run.cmd" --smoke
      RESULT_VARIABLE shipping_runtime_result
      OUTPUT_VARIABLE shipping_runtime_output
      ERROR_VARIABLE shipping_runtime_error
    )
  else()
    execute_process(
      COMMAND sh "${shipping_package_dir}/run.sh" --smoke
      RESULT_VARIABLE shipping_runtime_result
      OUTPUT_VARIABLE shipping_runtime_output
      ERROR_VARIABLE shipping_runtime_error
    )
  endif()
  if(NOT shipping_runtime_result EQUAL 0 OR
     NOT shipping_runtime_output MATCHES "gneiss.game.installed.workflow" OR
     NOT shipping_runtime_output MATCHES "stage=shutdown")
    message(FATAL_ERROR
            "Shipping 发布包启动失败：${shipping_runtime_result}\n${shipping_runtime_output}${shipping_runtime_error}"
    )
  endif()

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
     NOT lantern_output MATCHES "@gneiss-log-v1" OR
     NOT lantern_output MATCHES "\"source\":\"gneiss.examples.lantern_gallery\"" OR
     NOT lantern_output MATCHES "stage=shutdown")
    message(FATAL_ERROR
            "安装树 Lantern Gallery 启动失败：${lantern_result}\n${lantern_output}${lantern_error}")
  endif()
endif()
