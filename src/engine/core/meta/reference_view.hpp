// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
namespace gneiss::engine::meta {
namespace detail {
struct reference_view_access;
}
/** 借用引用槽位，不自动保活目标。重置/替换后须重新获取目标视图。 */
template <bool Mutable> class basic_reference_view {
  using target_type = std::conditional_t<Mutable, mutable_instance, instance>;
  friend class instance;
  friend class mutable_instance;
  friend struct detail::reference_view_access;
  template <bool> friend class basic_reference_view;
  explicit basic_reference_view(target_type target) : target_(std::move(target)) {}
  instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const reference_ops* ops() const noexcept {
    return readonly().access_->template get<reference_ops>();
  }
  target_type target_;
  result<ref<const void>> owner() const noexcept { return ops()->owner(readonly().data_); }
  error assign_owner(engine::type_id id, const ref<void>& owner) const noexcept
    requires(Mutable)
  {
    if (ops()->element().id != id)
      return error::invalid_argument;
    ops()->set(target_.data_, owner);
    return error::success;
  }

public:
  [[nodiscard]] result<bool> has_value() const noexcept {
    return ops()->get(readonly().data_) != nullptr;
  }
  /** ref<const T> 的可写视图仍不能修改目标，可经 as_const().get() 读取。 */
  [[nodiscard]] result<target_type> get() const noexcept {
    auto v = readonly();
    if constexpr (Mutable) {
      if (!ops()->get_mut)
        return error::unsupported;
      auto* p = ops()->get_mut(target_.data_);
      if (!p)
        return error::not_found;
      return mutable_instance::from_access(v.registry_, ops()->element(), p);
    } else {
      auto* p = ops()->get(v.data_);
      if (!p)
        return error::not_found;
      return instance::from_access(v.registry_, ops()->element(), p);
    }
  }
  [[nodiscard]] reference_view as_const() const noexcept { return reference_view{readonly()}; }
  /** 共享源控制块；仅允许相同引用类型，不复制目标。 */
  [[nodiscard]] error assign(const reference_view& source) const noexcept
    requires(Mutable)
  {
    return target_.assign(source.target_);
  }
  /** 只清空当前引用；最后一次释放遵循原删除器。 */
  [[nodiscard]] error reset() const noexcept
    requires(Mutable)
  {
    ops()->reset(target_.data_);
    return error::success;
  }
};
}
