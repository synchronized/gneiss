// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include <variant>
namespace gneiss::engine::meta {
using property_step = std::variant<property_id, std::size_t>;
using property_path = std::vector<property_step>;
/** 每次从根重新定位，不能跨容器修改保留元素地址。 */
template <class View> result<View> resolve_path(View root, std::span<const property_step> path) {
  for (const auto& step : path) {
    result<View> next = error::unsupported;
    if (const auto* property = std::get_if<property_id>(&step))
      next = root.property(*property);
    else {
      auto sequence = root.as_sequence();
      if (!sequence)
        return sequence.error();
      next = sequence->at(std::get<std::size_t>(step));
    }
    if (!next)
      return next.error();
    root = std::move(*next);
  }
  return root;
}
}
