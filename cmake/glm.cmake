# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors
include_guard(GLOBAL)
include(dependency_source_cache)
gneiss_cached_archive(glm
  "https://codeload.github.com/g-truc/glm/tar.gz/0af55ccecd98d4e5a8d1fad7de25ba429d60e863"
  "e7f187d83523f505eb38dd25d297ea6c0d4ed856d733e808f18253f5a8fa88a0"
  CMakeLists.txt GNEISS_GLM_SOURCE_DIR)
# 只使用锁定版本的头文件，不构建或安装 GLM SDK。
add_library(gneiss_math_dependency INTERFACE)
target_include_directories(gneiss_math_dependency SYSTEM INTERFACE "${GNEISS_GLM_SOURCE_DIR}")
