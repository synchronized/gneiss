# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

function(gneiss_resolve_granit_window)
  if(TARGET granit::window)
    return()
  endif()

  find_package(granit 0.3 CONFIG REQUIRED COMPONENTS Window)
  if(NOT TARGET granit::window)
    message(FATAL_ERROR "Granit Window 组件未提供 granit::window 目标")
  endif()
endfunction()
