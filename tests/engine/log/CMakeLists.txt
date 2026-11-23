# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_executable(gneiss_diagnostic_log_test diagnostic_log_test.cpp)
target_link_libraries(gneiss_diagnostic_log_test PRIVATE gneiss::engine)
target_compile_features(gneiss_diagnostic_log_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_diagnostic_log_test)
gneiss_target_output_directories(gneiss_diagnostic_log_test)
set_target_properties(gneiss_diagnostic_log_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.diagnostic_log COMMAND gneiss_diagnostic_log_test)
set_tests_properties(gneiss.diagnostic_log PROPERTIES LABELS "cpu;integration")

add_executable(
  gneiss_log_queue_test
  async_queue_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/core/logging/detail/async_queue.cpp"
)
target_include_directories(gneiss_log_queue_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(gneiss_log_queue_test PRIVATE gneiss_base gneiss_libassert Threads::Threads)
target_compile_features(gneiss_log_queue_test PRIVATE cxx_std_20)
gneiss_target_compile_warnings(gneiss_log_queue_test)
gneiss_target_output_directories(gneiss_log_queue_test)
set_target_properties(gneiss_log_queue_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.log-queue COMMAND gneiss_log_queue_test)
set_tests_properties(gneiss.log-queue PROPERTIES LABELS "cpu;integration")

add_executable(gneiss_logging_service_test logging_service_test.cpp)
target_link_libraries(gneiss_logging_service_test PRIVATE gneiss_logging gneiss_libassert)
gneiss_target_compile_warnings(gneiss_logging_service_test)
gneiss_target_output_directories(gneiss_logging_service_test)
set_target_properties(gneiss_logging_service_test PROPERTIES FOLDER "Tests")
add_test(NAME gneiss.logging-service COMMAND gneiss_logging_service_test)
set_tests_properties(gneiss.logging-service PROPERTIES LABELS "cpu;integration")
set_tests_properties(gneiss.logging-service PROPERTIES TIMEOUT 15)

foreach(mode IN ITEMS oom unexpected)
  add_test(NAME gneiss.log-callback-fatal-${mode}
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gneiss_log_queue_test>
    -DMODE=${mode} -P ${PROJECT_SOURCE_DIR}/tests/engine/core/check_fatal.cmake)
  set_tests_properties(gneiss.log-callback-fatal-${mode} PROPERTIES LABELS "cpu;integration;death")
endforeach()

add_test(NAME gneiss.logging-duplicate-binding-contract COMMAND gneiss_logging_contract_test duplicate)
set_tests_properties(gneiss.logging-duplicate-binding-contract PROPERTIES LABELS "cpu;integration;death")

# libassert 以静态库构建；失败处理器必须与被测断言在同一模块。
# 仅此死亡测试夹具编译本地实现，普通服务测试链接生产 logging 模块。
add_executable(gneiss_logging_contract_test logging_service_test.cpp
  "${PROJECT_SOURCE_DIR}/engine/src/core/logging/service.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/core/logging/logger.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/core/logging/sink_connection.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/core/logging/default_service.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/core/logging/detail/async_queue.cpp")
target_compile_definitions(gneiss_logging_contract_test PRIVATE GNEISS_LOGGING_STATIC_DEFINE)
target_link_libraries(gneiss_logging_contract_test PRIVATE gneiss_base gneiss_libassert Threads::Threads)
gneiss_target_compile_warnings(gneiss_logging_contract_test)
gneiss_target_output_directories(gneiss_logging_contract_test)
set_target_properties(gneiss_logging_contract_test PROPERTIES FOLDER "Tests")
set_tests_properties(gneiss.logging-duplicate-binding-contract PROPERTIES TIMEOUT 15)
