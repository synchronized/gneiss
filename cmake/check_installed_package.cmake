# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_var IN ITEMS GNEISS_BUILD_DIR GNEISS_SOURCE_DIR GNEISS_GENERATOR)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少安装 Consumer 验收参数：${required_var}")
  endif()
endforeach()

set(install_dir "${GNEISS_BUILD_DIR}/consumer-prefix")
set(consumer_build_dir "${GNEISS_BUILD_DIR}/consumer-build")
set(consumer_source_dir "${GNEISS_BUILD_DIR}/consumer-source")
file(REMOVE_RECURSE "${install_dir}" "${consumer_build_dir}" "${consumer_source_dir}")

# 先把 Consumer 及其输入复制出源码树，确保后续配置、构建和运行不隐式依赖仓库路径。
file(MAKE_DIRECTORY "${consumer_source_dir}/tests" "${consumer_source_dir}/examples/property_inspector")
file(COPY "${GNEISS_SOURCE_DIR}/tests/consumer" DESTINATION "${consumer_source_dir}/tests")
file(COPY "${GNEISS_SOURCE_DIR}/tests/data" DESTINATION "${consumer_source_dir}/tests")
file(COPY "${GNEISS_SOURCE_DIR}/examples/property_inspector/main.cpp"
     DESTINATION "${consumer_source_dir}/examples/property_inspector"
)
file(COPY "${GNEISS_SOURCE_DIR}/examples/property_inspector/assets"
     DESTINATION "${consumer_source_dir}/examples/property_inspector"
)

set(install_command "${CMAKE_COMMAND}" --install "${GNEISS_BUILD_DIR}" --prefix "${install_dir}")
set(build_command "${CMAKE_COMMAND}" --build "${consumer_build_dir}")
set(test_command "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build_dir}" --output-on-failure)
if(GNEISS_CONFIG)
  list(APPEND install_command --config "${GNEISS_CONFIG}")
  list(APPEND build_command --config "${GNEISS_CONFIG}")
  list(APPEND test_command -C "${GNEISS_CONFIG}")
endif()

execute_process(COMMAND ${install_command} RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Gneiss 安装失败：${install_result}")
endif()

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${consumer_source_dir}/tests/consumer"
    -B "${consumer_build_dir}"
    -G "${GNEISS_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${install_dir}"
)
if(GNEISS_GENERATOR_PLATFORM)
  list(APPEND configure_command -A "${GNEISS_GENERATOR_PLATFORM}")
endif()
if(GNEISS_BUILD_TYPE)
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${GNEISS_BUILD_TYPE}")
endif()
if(GNEISS_C_COMPILER)
  list(APPEND configure_command "-DCMAKE_C_COMPILER=${GNEISS_C_COMPILER}")
endif()
if(GNEISS_CXX_COMPILER)
  list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${GNEISS_CXX_COMPILER}")
endif()

execute_process(COMMAND ${configure_command} RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "安装 Consumer 配置失败：${configure_result}")
endif()
execute_process(COMMAND ${build_command} RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "安装 Consumer 构建失败：${build_result}")
endif()
execute_process(COMMAND ${test_command} RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "安装 Consumer 测试失败：${test_result}")
endif()
