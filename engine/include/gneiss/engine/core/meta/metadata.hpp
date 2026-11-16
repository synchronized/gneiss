// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/ref.hpp>
#include <gneiss/engine/core/types/type_id.hpp>

#include <vector>

namespace gneiss::meta {
/** 注册扩展元数据。按 C++ 类型区分，保存独立不可变值；Meta 不解释其内容。
 * 复制集合共享只读值，添加新项不修改已有副本。元数据内的借用指针仍由提供者负责寿命。 */
class metadata final {
  struct entry {
    gneiss::type_id id;
    ref<const void> value;
  };
  std::vector<entry> entries_;

public:
  [[nodiscard]] const void* find(gneiss::type_id id) const noexcept {
    for (const auto& item : entries_)
      if (item.id == id)
        return item.value.get();
    return nullptr;
  }
  template <class T> [[nodiscard]] const T* find() const noexcept {
    return static_cast<const T*>(find(runtime_type_of<T>()));
  }
  /** 同类元数据重复登记报错，不覆盖已登记的策略。 */
  template <class T> error insert(T value) {
    const auto id = runtime_type_of<T>();
    if (find(id))
      return error::invalid_argument;
    entries_.push_back({id, make_ref<const T>(std::move(value))});
    return error::success;
  }
};
}
