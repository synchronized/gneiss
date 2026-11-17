# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

add_subdirectory(engine)
if(TARGET gneiss_editor_ui)
  add_subdirectory(editor)
endif()
