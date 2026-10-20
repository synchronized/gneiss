# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

if(NOT GNEISS_SOURCE_DIR OR NOT GNEISS_BINARY_DIR)
  message(FATAL_ERROR "测试需要源码及构建目录")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run)
set(root "${GNEISS_BINARY_DIR}/cmake-tests/source-cache-${run}")
file(MAKE_DIRECTORY "${root}/fixture/demo")
file(WRITE "${root}/fixture/demo/CMakeLists.txt" "# fixture\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${root}/demo.tar.gz" demo
  WORKING_DIRECTORY "${root}/fixture" RESULT_VARIABLE packed)
if(NOT packed EQUAL 0)
  message(FATAL_ERROR "创建测试归档失败")
endif()
file(SHA256 "${root}/demo.tar.gz" hash)
set(child_template [=[
include("@GNEISS_SOURCE_DIR@/cmake/dependency_source_cache.cmake")
gneiss_cached_archive(demo "file://@root@/demo.tar.gz" ${HASH} CMakeLists.txt source)
file(WRITE "@root@/result-${ID}" "${source}")
]=])
string(CONFIGURE "${child_template}" child @ONLY)
file(WRITE "${root}/child.cmake" "${child}")

function(check_run cache offline expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DGNEISS_DEPENDENCY_SOURCE_CACHE_DIR=${cache}"
    "-DGNEISS_DEPENDENCY_OFFLINE=${offline}" "-DHASH=${hash}" -DID=single
    -P "${root}/child.cmake" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE error)
  if(expected STREQUAL "success" AND NOT status EQUAL 0)
    message(FATAL_ERROR "缓存应成功：${out} ${error}")
  elseif(expected STREQUAL "failure" AND status EQUAL 0)
    message(FATAL_ERROR "缓存应拒绝无效输入")
  endif()
endfunction()
check_run("${root}/cache" OFF success)
file(READ "${root}/result-single" first)
check_run("${root}/cache" ON success)
file(READ "${root}/result-single" second)
if(NOT first STREQUAL second)
  message(FATAL_ERROR "缓存命中目录不同")
endif()
check_run("${root}/missing" ON failure)
# 缓存只有归档也应支持离线解压。
file(MAKE_DIRECTORY "${root}/archive-only/archives")
file(COPY_FILE "${root}/demo.tar.gz" "${root}/archive-only/archives/demo-${hash}.tar.gz")
check_run("${root}/archive-only" ON success)
file(MAKE_DIRECTORY "${root}/corrupt/archives")
file(WRITE "${root}/corrupt/archives/demo-${hash}.tar.gz" "corrupt")
check_run("${root}/corrupt" OFF failure)
set(saved_hash "${hash}")
set(hash "0000000000000000000000000000000000000000000000000000000000000000")
check_run("${root}/bad-download" OFF failure)
set(hash "${saved_hash}")
# execute_process 的多个 COMMAND 同时运行；两者使用独立输出文件。
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_DEPENDENCY_SOURCE_CACHE_DIR=${root}/parallel"
    "-DHASH=${hash}" -DID=a -P "${root}/child.cmake"
  COMMAND "${CMAKE_COMMAND}" "-DGNEISS_DEPENDENCY_SOURCE_CACHE_DIR=${root}/parallel"
    "-DHASH=${hash}" -DID=b -P "${root}/child.cmake"
  RESULTS_VARIABLE statuses ERROR_VARIABLE errors)
if(NOT statuses STREQUAL "0;0")
  message(FATAL_ERROR "并发缓存失败：${statuses} ${errors}")
endif()
file(READ "${root}/result-a" a)
file(READ "${root}/result-b" b)
if(NOT a STREQUAL b OR NOT EXISTS "${a}/.gneiss-source-complete")
  message(FATAL_ERROR "并发未发布同一完整目录")
endif()
# 相同库名的新内容必须进入另一目录，旧版本保留。
file(APPEND "${root}/fixture/demo/CMakeLists.txt" "# version two\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${root}/demo.tar.gz" demo
  WORKING_DIRECTORY "${root}/fixture" COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${root}/demo.tar.gz" hash)
check_run("${root}/cache" OFF success)
file(READ "${root}/result-single" newer)
if(newer STREQUAL first OR NOT EXISTS "${first}/CMakeLists.txt")
  message(FATAL_ERROR "版本缓存没有隔离")
endif()
message(STATUS "源码缓存首次填充、复用、离线、损坏拒绝和并发测试通过")
