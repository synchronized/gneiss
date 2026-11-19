// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta {
/** 借用引用槽位，不自动保活目标。重置/替换后须重新获取目标视图。 */
template <bool Mutable> class basic_reference_view {
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  template <bool> friend class basic_instance;
  template <type_kind, template <bool> class> friend class cap::container;
  template <bool> friend class basic_reference_view;
  explicit basic_reference_view(target_type target) : target_(std::move(target)) {}
  const_instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const reference_ops* ops() const noexcept {
    return readonly().access()->template get<reference_ops>();
  }
  target_type target_;

public:
  /** 当前目标的运行类型；空引用仍遵循引用适配器的声明类型约定。 */
  meta::type actual_type() const {
    return ops()->dynamic_type(readonly().registry(), readonly().data_);
  }
  /** 显式共享控制块，返回的只读强引用保活目标。 */
  ref<const void> owner() const { return ops()->owner(readonly().data_); }
  /** 类型擦除赋值：调用方保证 owner 地址与 source_type 匹配，必须与声明目标类型具有相同运行身份。
   */
  error assign_owner(meta::type source_type, const ref<void>& owner) const
    requires(Mutable)
  {
    if (!source_type || ops()->element().id != source_type.id())
      return error::invalid_argument;
    ops()->set(target_.mutable_data(), owner);
    return error::success;
  }

public:
  [[nodiscard]] bool has_value() const { return ops()->const_get(readonly().data_) != nullptr; }
  /** ref<const T> 的可写视图仍不能修改目标，可经 as_const().get() 读取。 */
  [[nodiscard]] result<target_type> get() const {
    auto v = readonly();
    if constexpr (Mutable) {
      if (!ops()->get)
        return error::unsupported;
      auto* p = ops()->get(target_.mutable_data());
      if (!p)
        return error::not_found;
      auto owned = owner();
      return instance::from_object(actual_type(), const_cast<void*>(owned.get()));
    } else {
      auto* p = ops()->const_get(v.data_);
      if (!p)
        return error::not_found;
      auto owned = owner();
      return const_instance::from_object(actual_type(), owned.get());
    }
  }
  [[nodiscard]] const_reference_view as_const() const noexcept {
    return const_reference_view{readonly()};
  }
  /** 共享源控制块；仅允许相同引用类型，不复制目标。 */
  [[nodiscard]] error assign(const const_reference_view& source) const
    requires(Mutable)
  {
    return target_.assign(source.target_);
  }
  /** 只清空当前引用；最后一次释放遵循原删除器。 */
  void reset() const
    requires(Mutable)
  {
    ops()->reset(target_.mutable_data());
  }
};
}
