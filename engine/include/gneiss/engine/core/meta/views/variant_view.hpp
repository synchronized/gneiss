// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta {
/** 借用当前分支；select 后所有旧分支视图失效。未配置标签不影响内存访问。 */
template <bool Mutable> class basic_variant_view {
  template <bool> friend class basic_variant_view;
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  template <bool> friend class basic_instance;
  template <type_kind, template <bool> class> friend class cap::container;
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
  /** 借用同一目标并移除写入能力，不复制目标数据。 */
  [[nodiscard]] basic_variant_view<false> as_const() const noexcept {
    return basic_variant_view<false>{target_.as_const()};
  }
  bool valueless() const { return ops()->valueless(readonly().data_); }
  result<std::size_t> index() const {
    if (valueless())
      return error::invalid_state;
    return ops()->index(readonly().data_);
  }
  result<target_type> value() const {
    auto i = index();
    if (!i)
      return i.error();
    auto view = readonly();
    if constexpr (Mutable)
      return instance::from_access(view.registry(), *ops()->alternatives[*i],
                                   ops()->get_mut(target_.mutable_data()));
    else
      return const_instance::from_access(view.registry(), *ops()->alternatives[*i],
                                         ops()->get(view.data_));
  }
  error select(std::size_t i) const
    requires(Mutable)
  {
    return ops()->select(target_.mutable_data(), i);
  }
};
}
