// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta {
/** 借用视图；不延长目标寿命，结构修改后需重新获取元素视图。 */
template <bool Mutable> class basic_optional_view {
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  template <bool> friend class basic_instance;
  explicit basic_optional_view(target_type target) : target_(std::move(target)) {}
  const_instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const optional_ops* ops() const noexcept {
    return readonly().access()->template get<optional_ops>();
  }
  target_type target_;

public:
  [[nodiscard]] result<bool> has_value() const { return ops()->has_value(readonly().data_); }
  [[nodiscard]] result<target_type> get() const {
    auto v = readonly();
    if (!ops()->has_value(v.data_))
      return error::not_found;
    if constexpr (Mutable) {
      if (!ops()->get_mut)
        return error::unsupported;
      return instance::from_access(v.registry(), ops()->element(),
                                   ops()->get_mut(target_.mutable_data()));
    } else {
      return const_instance::from_access(v.registry(), ops()->element(), ops()->get(v.data_));
    }
  }
  [[nodiscard]] error reset() const
    requires(Mutable)
  {
    if (!ops()->reset)
      return error::unsupported;
    ops()->reset(target_.mutable_data());
    return error::success;
  }
  [[nodiscard]] result<target_type> ensure() const
    requires(Mutable)
  {
    if (!ops()->has_value(readonly().data_)) {
      if (!ops()->ensure)
        return error::unsupported;
      ops()->ensure(target_.mutable_data());
    }
    return get();
  }
};
}
