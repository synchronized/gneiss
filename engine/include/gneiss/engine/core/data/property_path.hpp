// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/data/property_id.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::meta {
/** 字段 ID、位置下标、映射字符串键、序列稳定身份。 */
using property_step = std::variant<property_id, std::size_t, std::string, guid>;
using property_path = std::vector<property_step>;
inline bool property_step_less(const property_step& a, const property_step& b) {
  if (a.index() != b.index())
    return a.index() < b.index();
  return std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, property_id>)
          return v.get() < std::get<T>(b).get();
        else
          return v < std::get<T>(b);
      },
      a);
}
inline std::string property_step_text(const property_step& step) {
  return std::visit(
      [](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, property_id>)
          return "/properties/" + std::to_string(v.get());
        else if constexpr (std::is_same_v<T, std::size_t>)
          return "/indices/" + std::to_string(v);
        else if constexpr (std::is_same_v<T, guid>)
          return "/elements/" + v.string();
        else
          return "/keys/" + std::to_string(v.size()) + ":" + v;
      },
      step);
}
inline std::string property_path_text(std::span<const property_step> path) {
  std::string text;
  for (const auto& step : path)
    text += property_step_text(step);
  return text;
}
}
