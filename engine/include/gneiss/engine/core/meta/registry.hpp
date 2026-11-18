// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/meta/ops/type_ops.hpp>
#include <gneiss/engine/core/meta/registry_fwd.hpp>
#include <gneiss/engine/core/meta/type.hpp>
#include <gneiss/engine/core/types/type_id.hpp>
#include <gneiss/engine/result.hpp>

#include <memory>

namespace gneiss::meta {
/** 查询冻结注册表；未发布或未找到返回空句柄，基础值不需重复登记。 */
GNEISS_API meta::type find_type(const registry_ref&, gneiss::type_id);
GNEISS_API meta::type find_type(const registry_ref&, gneiss::guid);
template <class T> meta::type type_of(const registry_ref& registry) {
  return detail::resolve_type(registry, detail::ops_of<std::remove_cvref_t<T>>());
}
/** 创建独立注册表并返回共享所有者；分配异常传播到异常边界。 */
[[nodiscard]] GNEISS_API registry_ref create_registry();
/** 进程共享的默认注册表；仅创建，不自动注册或冻结，由引擎模块持有至退出。 */
[[nodiscard]] GNEISS_API registry_ref default_registry();
[[nodiscard]] GNEISS_API error freeze(const registry_ref& state);
/** 查询冻结状态；注册表必须有效，空表属于契约错误。 */
[[nodiscard]] GNEISS_API bool is_frozen(const registry_ref& state);
/** 冻结注册对象的只读范围；保活注册表，不复制类型列表，不包含枚举注册项。 */
class type_range {
  friend class type_registry_state;
  registry_ref registry_;
  const std::unique_ptr<detail::type_record>* records_{};
  std::size_t size_{};
  type_range(registry_ref registry, const std::unique_ptr<detail::type_record>* records,
             std::size_t size)
      : registry_(std::move(registry)), records_(records), size_(size) {}

public:
  type_range() = default;
  class iterator {
    friend class type_range;
    registry_ref registry_;
    const std::unique_ptr<detail::type_record>* records_{};
    std::size_t index_{};
    iterator(registry_ref registry, const std::unique_ptr<detail::type_record>* records,
             std::size_t index)
        : registry_(std::move(registry)), records_(records), index_(index) {}

  public:
    using value_type = meta::type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    iterator() = default;
    [[nodiscard]] GNEISS_API meta::type operator*() const noexcept;
    iterator& operator++() noexcept {
      ++index_;
      return *this;
    }
    iterator operator++(int) noexcept {
      auto old = *this;
      ++*this;
      return old;
    }
    friend bool operator==(const iterator&, const iterator&) = default;
  };
  [[nodiscard]] iterator begin() const noexcept { return {registry_, records_, 0}; }
  [[nodiscard]] iterator end() const noexcept { return {registry_, records_, size_}; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  /** 下标必须小于 size()；越界属于调用契约错误。 */
  [[nodiscard]] GNEISS_API meta::type operator[](std::size_t) const noexcept;
};
/** 按 GUID 字节顺序借用注册对象；未冻结返回 not_ready。 */
[[nodiscard]] GNEISS_API result<type_range> registered_types(const registry_ref&);
template <class T> meta::type type_of() { return type_of<T>(default_registry()); }
inline meta::type find_type(gneiss::type_id id) { return find_type(default_registry(), id); }
inline meta::type find_type(gneiss::guid id) { return find_type(default_registry(), id); }
[[nodiscard]] inline error freeze() { return freeze(default_registry()); }
[[nodiscard]] inline bool is_frozen() { return is_frozen(default_registry()); }
[[nodiscard]] inline result<type_range> registered_types() {
  return registered_types(default_registry());
}

} // namespace gneiss::meta
