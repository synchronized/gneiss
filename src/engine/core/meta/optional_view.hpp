// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
namespace gneiss::engine::meta {
/** 借用视图；不延长目标寿命，结构修改后需重新获取元素视图。 */
template <bool Mutable> class basic_optional_view {
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  friend class const_instance;
  friend class instance;
  explicit basic_optional_view(target_type target) : target_(std::move(target)) {}
  const_instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const optional_ops* ops() const noexcept {
    return readonly().access_->template get<optional_ops>();
  }
  target_type target_;

public:
  [[nodiscard]] result<bool> has_value() const noexcept try {
    return ops()->has_value(readonly().data_);
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> get() const noexcept try {
    auto v = readonly();
    if (!ops()->has_value(v.data_))
      return error::not_found;
    if constexpr (Mutable) {
      if (!ops()->get_mut)
        return error::unsupported;
      return instance::from_access(v.registry_, ops()->element(), ops()->get_mut(target_.data_));
    } else {
      return const_instance::from_access(v.registry_, ops()->element(), ops()->get(v.data_));
    }
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error reset() const noexcept
    requires(Mutable)
  try {
    if (!ops()->reset)
      return error::unsupported;
    ops()->reset(target_.data_);
    return error::success;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> ensure() const noexcept
    requires(Mutable)
  try {
    if (!ops()->has_value(readonly().data_)) {
      if (!ops()->ensure)
        return error::unsupported;
      ops()->ensure(target_.data_);
    }
    return get();
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
};
}
