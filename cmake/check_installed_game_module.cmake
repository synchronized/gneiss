# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_var IN ITEMS GNEISS_BUILD_DIR GNEISS_SOURCE_DIR GNEISS_GENERATOR)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少游戏模块安装验收参数：${required_var}")
  endif()
endforeach()

set(install_dir "${GNEISS_BUILD_DIR}/game-module-prefix")
set(consumer_source_dir "${GNEISS_BUILD_DIR}/game-module-source")
set(consumer_build_dir "${GNEISS_BUILD_DIR}/game-module-build")
file(REMOVE_RECURSE "${install_dir}" "${consumer_source_dir}" "${consumer_build_dir}")

# 模板使用最小游戏模块协议，复制到源码树外验证安装入口。
file(MAKE_DIRECTORY "${consumer_source_dir}/templates")
file(COPY "${GNEISS_SOURCE_DIR}/templates/game" DESTINATION "${consumer_source_dir}/templates")

set(install_command "${CMAKE_COMMAND}" --install "${GNEISS_BUILD_DIR}" --prefix "${install_dir}")
set(build_command "${CMAKE_COMMAND}" --build "${consumer_build_dir}")
if(GNEISS_CONFIG)
  list(APPEND install_command --config "${GNEISS_CONFIG}")
  list(APPEND build_command --config "${GNEISS_CONFIG}")
endif()

execute_process(COMMAND ${install_command} RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Gneiss 游戏模块安装失败：${install_result}")
endif()

foreach(
  required_document
  IN ITEMS
    LICENSE
    CHANGELOG.md
    THIRD_PARTY_NOTICES.md
    licenses/caors-core-values-LICENSE
    licenses/EnTT-LICENSE
    licenses/yyjson-LICENSE
    licenses/libspng-LICENSE
    licenses/miniz-LICENSE
)
  if(NOT EXISTS "${install_dir}/share/doc/gneiss/${required_document}")
    message(FATAL_ERROR "Gneiss 游戏模块安装树缺少发布声明：${required_document}")
  endif()
endforeach()

foreach(
  required_render_asset
  IN ITEMS
    libraries/pbr_standard.grshlib
    materials/pbr_standard.grmat
)
  if(NOT EXISTS
     "${install_dir}/share/gneiss/runtime-assets/granit/${required_render_asset}"
  )
    message(FATAL_ERROR "Gneiss 安装树缺少 Granit 标准渲染资产：${required_render_asset}")
  endif()
endforeach()

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${consumer_source_dir}/templates/game"
    -B "${consumer_build_dir}"
    -G "${GNEISS_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${install_dir}"
    "-DGNEISS_GAME_OUTPUT_DIRECTORY=${consumer_build_dir}/module"
)
if(GNEISS_GENERATOR_PLATFORM)
  list(APPEND configure_command -A "${GNEISS_GENERATOR_PLATFORM}")
endif()
if(GNEISS_BUILD_TYPE)
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${GNEISS_BUILD_TYPE}")
endif()
if(GNEISS_CXX_COMPILER)
  list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${GNEISS_CXX_COMPILER}")
endif()

execute_process(COMMAND ${configure_command} RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "安装游戏模块 Consumer 配置失败：${configure_result}")
endif()
execute_process(COMMAND ${build_command} RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "安装游戏模块 Consumer 构建失败：${build_result}")
endif()
