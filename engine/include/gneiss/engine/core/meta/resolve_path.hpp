// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/instance.hpp>

#include <gneiss/engine/core/data/property_path.hpp>

namespace gneiss::meta {
/** 每次从根重新定位；持久化地址拒绝可变序列的位置下标。 */
template <class View>
result<View> resolve_path(View root, std::span<const property_step> path, bool persistent = false) {
  for (const auto& step : path) {
    result<View> next = error::unsupported;
    if (const auto* property = std::get_if<property_id>(&step)) {
      auto object = root.template capability<cap::object>();
      if (!object)
        return object.error();
      next = object->member(*property);
    } else if (const auto* key = std::get_if<std::string>(&step)) {
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
