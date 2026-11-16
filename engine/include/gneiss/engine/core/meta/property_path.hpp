// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>

#include <variant>

namespace gneiss::meta {
/** 字段 ID、位置下标、映射字符串键、序列稳定身份。 */
using property_step = std::variant<property_id, std::size_t, std::string, guid>;
template <> struct variant_case_traits<property_step> {
  static constexpr std::array<std::string_view, 4> names{"property", "index", "key", "element"};
};
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
/** 每次从根重新定位；持久化地址拒绝可变序列的位置下标。 */
template <class View>
result<View> resolve_path(View root, std::span<const property_step> path, bool persistent = false) {
  for (const auto& step : path) {
    result<View> next = error::unsupported;
    if (const auto* property = std::get_if<property_id>(&step))
      next = root.member(*property);
    else if (const auto* key = std::get_if<std::string>(&step)) {
      auto map = root.as_associative();
      if (!map)
        return map.error();
      next = map->find(*key);
    } else {
      auto sequence = root.as_sequence();
      if (!sequence)
        return sequence.error();
      if (const auto* id = std::get_if<guid>(&step))
        next = sequence->find(*id);
      else {
        if (persistent && !sequence->is_fixed_size())
          return error::unsupported;
        next = sequence->at(std::get<std::size_t>(step));
      }
    }
    if (!next)
      return next.error();
    root = std::move(*next);
  }
  return root;
}
}
