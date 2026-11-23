# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

function(gneiss_resolve_editor_fonts)
  set(font_directory "${CMAKE_BINARY_DIR}/_deps/gneiss_editor_fonts")
  set(font_path "${font_directory}/NotoSansSC-wght.ttf")
  file(MAKE_DIRECTORY "${font_directory}")
  file(
    DOWNLOAD
    "https://raw.githubusercontent.com/google/fonts/45b0855d499c093e4d1bd08926fec4e1a582e225/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf"
    "${font_path}"
    EXPECTED_HASH
      SHA256=a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da
    TLS_VERIFY ON
    STATUS download_status
  )
  list(GET download_status 0 download_result)
  list(GET download_status 1 download_message)
  if(NOT download_result EQUAL 0)
    message(FATAL_ERROR "下载 Noto Sans SC 失败：${download_message}")
  endif()
  set(GNEISS_EDITOR_CJK_FONT_PATH "${font_path}" PARENT_SCOPE)
endfunction()
