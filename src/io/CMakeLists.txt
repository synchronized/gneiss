# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_library(
  gneiss_uv_runtime STATIC
  ipc_frame.cpp
  ipc_frame.h
  ipc_protocol.cpp
  ipc_protocol.h
  ipc_transport.cpp
  ipc_transport.h
  uv_error.cpp
  uv_error.h
  uv_runtime.cpp
  uv_runtime.h
  uv_runtime_access.h
)
target_link_libraries(
  gneiss_uv_runtime
  PUBLIC gneiss::engine
  PRIVATE gneiss::libuv gneiss_yyjson
)
target_include_directories(gneiss_uv_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gneiss_uv_runtime PUBLIC cxx_std_20)
gneiss_target_compile_warnings(gneiss_uv_runtime)
gneiss_target_output_directories(gneiss_uv_runtime)

if(GNEISS_BUILD_TESTING)
  add_executable(gneiss_uv_runtime_test tests/uv_runtime_test.cpp)
  target_link_libraries(gneiss_uv_runtime_test PRIVATE gneiss_uv_runtime)
  target_compile_features(gneiss_uv_runtime_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_runtime_test)
  gneiss_target_output_directories(gneiss_uv_runtime_test)
  add_test(NAME gneiss.io.uv-runtime COMMAND gneiss_uv_runtime_test)

  add_executable(gneiss_uv_runtime_stress_test tests/uv_runtime_stress_test.cpp)
  target_link_libraries(gneiss_uv_runtime_stress_test PRIVATE gneiss_uv_runtime)
  target_compile_features(gneiss_uv_runtime_stress_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_runtime_stress_test)
  gneiss_target_output_directories(gneiss_uv_runtime_stress_test)
  add_test(NAME gneiss.io.uv-runtime-stress COMMAND gneiss_uv_runtime_stress_test)

  add_executable(gneiss_uv_error_test tests/uv_error_test.cpp)
  target_link_libraries(gneiss_uv_error_test PRIVATE gneiss_uv_runtime gneiss::libuv)
  target_compile_features(gneiss_uv_error_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_uv_error_test)
  gneiss_target_output_directories(gneiss_uv_error_test)
  add_test(NAME gneiss.io.uv-error COMMAND gneiss_uv_error_test)

  add_executable(gneiss_ipc_frame_test tests/ipc_frame_test.cpp)
  target_link_libraries(gneiss_ipc_frame_test PRIVATE gneiss_uv_runtime)
  target_compile_features(gneiss_ipc_frame_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_ipc_frame_test)
  gneiss_target_output_directories(gneiss_ipc_frame_test)
  add_test(NAME gneiss.io.ipc-frame COMMAND gneiss_ipc_frame_test)

  add_executable(gneiss_ipc_transport_test tests/ipc_transport_test.cpp)
  target_link_libraries(gneiss_ipc_transport_test PRIVATE gneiss_uv_runtime)
  target_compile_features(gneiss_ipc_transport_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_ipc_transport_test)
  gneiss_target_output_directories(gneiss_ipc_transport_test)
  add_test(NAME gneiss.io.ipc-transport COMMAND gneiss_ipc_transport_test)

  add_executable(gneiss_ipc_protocol_test tests/ipc_protocol_test.cpp)
  target_link_libraries(gneiss_ipc_protocol_test PRIVATE gneiss_uv_runtime)
  target_compile_features(gneiss_ipc_protocol_test PRIVATE cxx_std_20)
  gneiss_target_compile_warnings(gneiss_ipc_protocol_test)
  gneiss_target_output_directories(gneiss_ipc_protocol_test)
  add_test(NAME gneiss.io.ipc-protocol COMMAND gneiss_ipc_protocol_test)
endif()
