# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 编译警告只作用于 Gneiss 自有目标，不传递给第三方库或下游使用者。
option(GNEISS_ENABLE_WARNINGS "启用 Gneiss 目标编译警告" ON)
option(GNEISS_ENABLE_PEDANTIC_WARNINGS "启用 Gneiss 目标严格标准扩展警告" OFF)
option(GNEISS_WARNINGS_AS_ERRORS "将 Gneiss 目标编译警告视为错误" OFF)

function(gneiss_target_output_directories target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "目标不存在: ${target}")
  endif()

  if(CMAKE_CONFIGURATION_TYPES)
    set(runtime_directory "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    set(library_directory "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
  else()
    set(runtime_directory "${CMAKE_BINARY_DIR}/bin")
    set(library_directory "${CMAKE_BINARY_DIR}/lib")
  endif()

  set_target_properties(
    ${target}
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${runtime_directory}"
      LIBRARY_OUTPUT_DIRECTORY "${library_directory}"
      ARCHIVE_OUTPUT_DIRECTORY "${library_directory}"
  )
endfunction()

function(gneiss_target_compile_warnings target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "目标不存在: ${target}")
  endif()

  # Windows 编译器显式使用 UTF-8；该选项只作用于 Gneiss 自有目标。
  target_compile_options(
    ${target}
    PRIVATE
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/utf-8>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>
  )

  if(NOT GNEISS_ENABLE_WARNINGS)
    return()
  endif()

  target_compile_options(
    ${target}
    PRIVATE
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wextra>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wextra>
  )

  if(GNEISS_ENABLE_PEDANTIC_WARNINGS)
    target_compile_options(
      ${target}
      PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wpedantic>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wpedantic>
    )
  endif()

  if(NOT GNEISS_WARNINGS_AS_ERRORS)
    return()
  endif()

  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    set_property(TARGET ${target} PROPERTY COMPILE_WARNING_AS_ERROR ON)
    return()
  endif()

  target_compile_options(
    ${target}
    PRIVATE
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/WX>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Werror>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Werror>
  )
endfunction()

