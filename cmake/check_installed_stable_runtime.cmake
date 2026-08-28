# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_var IN ITEMS GNEISS_BUILD_DIR GNEISS_SOURCE_DIR GNEISS_GENERATOR)
  if(NOT DEFINED ${required_var})
    message(FATAL_ERROR "缺少稳定运行时安装验收参数：${required_var}")
  endif()
endforeach()

set(install_dir "${GNEISS_BUILD_DIR}/stable-runtime-prefix")
set(consumer_source_dir "${GNEISS_BUILD_DIR}/stable-runtime-source")
set(consumer_build_dir "${GNEISS_BUILD_DIR}/stable-runtime-build")
file(REMOVE_RECURSE "${install_dir}" "${consumer_source_dir}" "${consumer_build_dir}")

file(MAKE_DIRECTORY "${consumer_source_dir}/examples")
file(COPY "${GNEISS_SOURCE_DIR}/examples/stable_runtime"
     DESTINATION "${consumer_source_dir}/examples"
)
file(COPY "${GNEISS_SOURCE_DIR}/examples/temple" DESTINATION "${consumer_source_dir}/examples")

set(install_command "${CMAKE_COMMAND}" --install "${GNEISS_BUILD_DIR}" --prefix "${install_dir}")
if(GNEISS_GRANIT_BUILD_DIR)
  set(granit_build_dir "${GNEISS_GRANIT_BUILD_DIR}")
else()
  set(granit_build_dir "${GNEISS_BUILD_DIR}/_deps/gneiss_granit-build")
endif()
set(build_command "${CMAKE_COMMAND}" --build "${consumer_build_dir}")
set(test_command "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build_dir}" --output-on-failure)
if(GNEISS_CONFIG)
  list(APPEND install_command --config "${GNEISS_CONFIG}")
  list(APPEND build_command --config "${GNEISS_CONFIG}")
  list(APPEND test_command -C "${GNEISS_CONFIG}")
endif()

if(EXISTS "${granit_build_dir}/cmake_install.cmake")
  set(granit_install_command
      "${CMAKE_COMMAND}" --install "${granit_build_dir}" --prefix "${install_dir}"
  )
  if(GNEISS_CONFIG)
    list(APPEND granit_install_command --config "${GNEISS_CONFIG}")
  endif()
  execute_process(COMMAND ${granit_install_command} RESULT_VARIABLE granit_install_result)
  if(NOT granit_install_result EQUAL 0)
    message(FATAL_ERROR "测试前缀中的 Granit 安装失败：${granit_install_result}")
  endif()
endif()

execute_process(COMMAND ${install_command} RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Gneiss 稳定运行时安装失败：${install_result}")
endif()

string(REPLACE ";" "\\;" dependency_prefix_path "${GNEISS_DEPENDENCY_PREFIX_PATH}")
set(dependency_runtime_dirs "")
foreach(dependency_prefix IN LISTS GNEISS_DEPENDENCY_PREFIX_PATH)
  if(IS_DIRECTORY "${dependency_prefix}/bin")
    list(APPEND dependency_runtime_dirs "${dependency_prefix}/bin")
  endif()
endforeach()
string(REPLACE ";" "\\;" dependency_runtime_dirs "${dependency_runtime_dirs}")
set(configure_command
    "${CMAKE_COMMAND}"
    -S "${consumer_source_dir}/examples/stable_runtime"
    -B "${consumer_build_dir}"
    -G "${GNEISS_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${install_dir}\;${dependency_prefix_path}"
    "-DGNEISS_CONSUMER_RUNTIME_DIRS=${dependency_runtime_dirs}"
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
  message(FATAL_ERROR "稳定运行时 Consumer 配置失败：${configure_result}")
endif()
execute_process(COMMAND ${build_command} RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "稳定运行时 Consumer 构建失败：${build_result}")
endif()
execute_process(COMMAND ${test_command} RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "稳定运行时 Consumer 测试失败：${test_result}")
endif()
