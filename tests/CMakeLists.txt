# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

# 生产目标全部建立后，统一装配测试；模块选项决定对应测试是否存在。
add_subdirectory(integration/assets)
if(GNEISS_BUILD_TOOLS)
  add_subdirectory(tooling)
endif()
if(GNEISS_BUILD_EDITOR OR GNEISS_BUILD_RUNTIME)
  add_subdirectory(project)
  add_subdirectory(protocol)
endif()
if(GNEISS_BUILD_RUNTIME)
  add_subdirectory(runtime)
endif()
add_subdirectory(engine)
if(TARGET gneiss_editor_ui)
  add_subdirectory(editor)
endif()
