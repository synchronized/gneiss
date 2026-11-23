// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/instance.hpp>

#include <gneiss/engine/core/data/property_path.hpp>

namespace gneiss::meta {
/** 按路径读取 value；计算属性的临时存储在内部管理，不向外返回临时借用。 */
[[nodiscard]] GNEISS_META_API result<value>
read_path(const const_instance&, std::span<const property_step>, bool persistent = false);
/** 设置路径叶值；穿过受约束属性时复制当前候选，成功后经 setter 写回。
 * 复制遵循 C++ 语义，不保证 ref 指向对象的深拷贝或跨属性事务。 */
[[nodiscard]] GNEISS_META_API error write_path(const instance&, std::span<const property_step>,
                                               const value&, bool persistent = false);

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
        if (persistent && !root.type().template capability<cap::sequence>().is_fixed_size())
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
