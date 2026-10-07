# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

cmake_minimum_required(VERSION 3.23)

project(gneiss_consumer LANGUAGES C CXX)

include(CTest)
find_package(gneiss CONFIG REQUIRED)

# 必须在干净前缀验证：旧转发路径不可编译，失败原因必须确为缺少头文件。
function(gneiss_verify_removed_headers)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(removed_headers)
  foreach(module application asset game_module input log reflection render scene world)
    list(APPEND removed_headers "gneiss/${module}.h" "gneiss/${module}.hpp")
  endforeach()
  foreach(module entity result rid)
    list(APPEND removed_headers "gneiss/core/${module}.h" "gneiss/core/${module}.hpp")
  endforeach()
  list(APPEND removed_headers "gneiss/core/export.h" "gneiss/core/version.h")
  foreach(header IN LISTS removed_headers)
    string(MAKE_C_IDENTIFIER "${header}" probe)
    if(header MATCHES "\\.hpp$")
      set(extension cpp)
    else()
      set(extension c)
    endif()
    set(source "${CMAKE_CURRENT_BINARY_DIR}/removed_headers/${probe}.${extension}")
    file(WRITE "${source}" "#include <${header}>\ntypedef int gneiss_removed_header_probe;\n")
    set(probe_sources "${source}")
    if(extension STREQUAL "c")
      # 较旧 CMake 不会因导入目标的 cxx_std_20 自动启用 CXX；C 探针仍独立按 C11 编译。
      set(cxx_source "${CMAKE_CURRENT_BINARY_DIR}/removed_headers/${probe}_language.cpp")
      file(WRITE "${cxx_source}" "typedef int gneiss_removed_header_cxx_language;\n")
      list(APPEND probe_sources "${cxx_source}")
    endif()
    try_compile(old_header_compiles
      "${CMAKE_CURRENT_BINARY_DIR}/removed_headers/${probe}"
      SOURCES ${probe_sources}
      CMAKE_FLAGS "-DCMAKE_C_STANDARD=11" "-DCMAKE_CXX_STANDARD=20"
      LINK_LIBRARIES gneiss::engine
      OUTPUT_VARIABLE output)
    if(old_header_compiles)
      message(FATAL_ERROR "旧头路径仍可编译，请改用干净安装前缀：${header}")
    endif()
    if(NOT output MATCHES "[Ff]ile not found|[Nn]o such file or directory|[Cc]annot open include file")
      message(FATAL_ERROR "旧头负向检查不是因为缺少头文件而失败：${header}\n${output}")
    endif()
  endforeach()
endfunction()
gneiss_verify_removed_headers()

add_executable(gneiss_c_consumer c_consumer.c)
target_link_libraries(gneiss_c_consumer PRIVATE gneiss::engine)
set_target_properties(
  gneiss_c_consumer
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX
)

add_executable(gneiss_cpp_consumer cpp_consumer.cpp)
target_link_libraries(gneiss_cpp_consumer PRIVATE gneiss::engine)
target_compile_features(gneiss_cpp_consumer PRIVATE cxx_std_20)
target_compile_definitions(
  gneiss_cpp_consumer
  PRIVATE GNEISS_CONSUMER_ASSET_ROOT="${CMAKE_CURRENT_LIST_DIR}/../data/assets"
)

add_executable(
  gneiss_property_inspector_consumer
  "${CMAKE_CURRENT_LIST_DIR}/../../examples/property_inspector/main.cpp"
)
target_link_libraries(gneiss_property_inspector_consumer PRIVATE gneiss::engine)
target_compile_features(gneiss_property_inspector_consumer PRIVATE cxx_std_20)
target_compile_definitions(
  gneiss_property_inspector_consumer
  PRIVATE
    GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT="${CMAKE_CURRENT_LIST_DIR}/../../examples/property_inspector/assets"
)

add_test(NAME gneiss.consumer.c COMMAND gneiss_c_consumer)
add_executable(gneiss_application_world_consumer
  "${CMAKE_CURRENT_LIST_DIR}/../../examples/application_world/main.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../examples/application_world/scene_update.cpp")
target_link_libraries(gneiss_application_world_consumer PRIVATE gneiss::engine)
target_compile_features(gneiss_application_world_consumer PRIVATE cxx_std_20)
add_test(NAME gneiss.consumer.application_world COMMAND gneiss_application_world_consumer)
add_test(NAME gneiss.consumer.cpp COMMAND gneiss_cpp_consumer)
add_test(NAME gneiss.consumer.property_inspector COMMAND gneiss_property_inspector_consumer)
set_tests_properties(
  gneiss.consumer.c gneiss.consumer.cpp gneiss.consumer.property_inspector
  gneiss.consumer.application_world
  PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${GNEISS_RUNTIME_DIR}"
)
