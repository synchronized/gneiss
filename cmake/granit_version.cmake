# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

set(_gneiss_granit_git_tag_default "eb970c74570e278678ee39530c68afc40101879f")
set(_gneiss_granit_git_tag_previous_default "a126ef1ec825a50f7e58d6323bfef9583e44c085")

option(
  GNEISS_GRANIT_UPDATE_DEFAULTS
  "配置时将仍沿用旧项目默认值的 Granit 提交升级到当前默认值"
  ON
)

if(NOT DEFINED GNEISS_GRANIT_GIT_TAG)
  set(
    GNEISS_GRANIT_GIT_TAG
    "${_gneiss_granit_git_tag_default}"
    CACHE STRING
    "FETCH 模式锁定的 Granit Git tag 或完整提交"
  )
elseif(GNEISS_GRANIT_UPDATE_DEFAULTS)
  set(_gneiss_granit_cached_default "")
  if(DEFINED _GNEISS_GRANIT_GIT_TAG_DEFAULT)
    set(_gneiss_granit_cached_default "${_GNEISS_GRANIT_GIT_TAG_DEFAULT}")
  elseif(GNEISS_GRANIT_GIT_TAG STREQUAL _gneiss_granit_git_tag_previous_default)
    # 兼容引入默认值追踪前创建的构建目录。
    set(_gneiss_granit_cached_default "${_gneiss_granit_git_tag_previous_default}")
  endif()

  if(GNEISS_GRANIT_GIT_TAG STREQUAL _gneiss_granit_cached_default)
    set(
      GNEISS_GRANIT_GIT_TAG
      "${_gneiss_granit_git_tag_default}"
      CACHE STRING
      "FETCH 模式锁定的 Granit Git tag 或完整提交"
      FORCE
    )
  endif()
endif()

set(
  _GNEISS_GRANIT_GIT_TAG_DEFAULT
  "${_gneiss_granit_git_tag_default}"
  CACHE INTERNAL
  "上次配置时 Gneiss 提供的 Granit 默认提交"
  FORCE
)
