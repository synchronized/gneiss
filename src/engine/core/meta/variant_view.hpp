// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
namespace gneiss::engine::meta {
/** 借用当前分支；select 后所有旧分支视图失效。未配置标签不影响内存访问。 */
template <bool Mutable> class basic_variant_view {
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  friend class const_instance;
  friend class instance;
  explicit basic_variant_view(target_type target) : target_(std::move(target)) {}
  const_instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const variant_ops* ops() const noexcept {
    return readonly().access()->template get<variant_ops>();
  }
  target_type target_;

public:
  std::size_t alternative_count() const noexcept { return ops()->alternatives.size(); }
  std::span<const std::string_view> names() const noexcept { return ops()->names; }
  result<meta::type> alternative(std::size_t i) const noexcept {
    if (i >= alternative_count())
      return error::invalid_argument;
    return detail::resolve_type(readonly().registry(), *ops()->alternatives[i]);
  }
  bool valueless() const noexcept { return ops()->valueless(readonly().data_); }
  result<std::size_t> index() const noexcept {
    if (valueless())
      return error::invalid_state;
    return ops()->index(readonly().data_);
  }
  result<target_type> value() const noexcept try {
    auto i = index();
    if (!i)
      return i.error();
    auto view = readonly();
    if constexpr (Mutable)
      return instance::from_access(view.registry(), *ops()->alternatives[*i],
                                   ops()->get_mut(target_.data_));
    else
      return const_instance::from_access(view.registry(), *ops()->alternatives[*i],
                                         ops()->get(view.data_));
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  error select(std::size_t i) const noexcept
    requires(Mutable)
  try {
    return ops()->select(target_.data_, i);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
};
}
