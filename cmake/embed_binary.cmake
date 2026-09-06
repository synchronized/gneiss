# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "embed_binary.cmake 需要 INPUT 和 OUTPUT")
endif()

file(READ "${INPUT}" content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")
file(WRITE "${OUTPUT}" "${content}\n")
