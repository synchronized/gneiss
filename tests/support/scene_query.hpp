// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document.hpp"
#include <stdexcept>
namespace gneiss::test_support {
inline editor::scene_query_snapshot scene_query(const editor::scene_document& document) {
  if (!document.is_open())
    return {};
  auto snapshot = document.capture_nodes();
  if (!snapshot)
    throw std::runtime_error("场景快照查询失败");
  return std::move(*snapshot);
}
}
