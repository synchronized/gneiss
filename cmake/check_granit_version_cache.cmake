# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

foreach(required_variable IN ITEMS GNEISS_SOURCE_DIR GNEISS_BINARY_DIR)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "缺少 ${required_variable}")
  endif()
endforeach()

set(fixture_dir "${GNEISS_SOURCE_DIR}/cmake/tests/granit_version_fixture")
set(test_root "${GNEISS_BINARY_DIR}/cmake-tests/granit-version-cache")
set(current_default "fa42c5f479ff98642b42f4cf31c77bc1932715f4")
set(previous_default "eb970c74570e278678ee39530c68afc40101879f")
set(custom_override "0123456789abcdef0123456789abcdef01234567")
file(REMOVE_RECURSE "${test_root}")

function(configure_fixture build_dir)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -S "${fixture_dir}" -B "${build_dir}"
      "-DGNEISS_SOURCE_DIR=${GNEISS_SOURCE_DIR}" ${ARGN}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
  )
  if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "测试配置失败：\n${configure_output}\n${configure_error}")
  endif()
endfunction()

function(assert_cache_value build_dir variable expected)
  file(STRINGS "${build_dir}/CMakeCache.txt" cache_lines REGEX "^${variable}(:[^=]+)?=")
  if(NOT cache_lines)
    message(FATAL_ERROR "缓存中缺少 ${variable}")
  endif()
  list(GET cache_lines 0 cache_line)
  string(REGEX REPLACE "^[^=]+=" "" actual "${cache_line}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${variable} 期望为 ${expected}，实际为 ${actual}")
  endif()
endfunction()

set(fresh_dir "${test_root}/fresh")
configure_fixture("${fresh_dir}")
assert_cache_value("${fresh_dir}" GNEISS_GRANIT_GIT_TAG "${current_default}")

set(upgrade_dir "${test_root}/upgrade")
configure_fixture(
  "${upgrade_dir}"
  "-DGNEISS_GRANIT_GIT_TAG=${previous_default}"
)
assert_cache_value("${upgrade_dir}" GNEISS_GRANIT_GIT_TAG "${current_default}")

set(pinned_previous_dir "${test_root}/pinned-previous")
configure_fixture(
  "${pinned_previous_dir}"
  "-DGNEISS_GRANIT_GIT_TAG=${previous_default}"
  -DGNEISS_GRANIT_UPDATE_DEFAULTS=OFF
)
assert_cache_value("${pinned_previous_dir}" GNEISS_GRANIT_GIT_TAG "${previous_default}")

set(override_dir "${test_root}/override")
configure_fixture(
  "${override_dir}"
  "-DGNEISS_GRANIT_GIT_TAG=${custom_override}"
  "-D_GNEISS_GRANIT_GIT_TAG_DEFAULT=${previous_default}"
)
assert_cache_value("${override_dir}" GNEISS_GRANIT_GIT_TAG "${custom_override}")
