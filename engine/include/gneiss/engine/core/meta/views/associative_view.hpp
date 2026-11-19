// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta {
/** 借用视图；不延长目标寿命，结构修改后需重新获取元素视图。 */
template <bool Mutable> class basic_associative_view {
  template <bool> friend class basic_associative_view;
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  template <bool> friend class basic_instance;
  template <type_kind, template <bool> class> friend class cap::container;
  explicit basic_associative_view(target_type target) : target_(std::move(target)) {}
  const_instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const associative_ops* ops() const noexcept {
    return readonly().access()->template get<associative_ops>();
  }
  target_type target_;

public:
  /** 借用同一目标并移除写入能力，不复制目标数据。 */
  [[nodiscard]] basic_associative_view<false> as_const() const noexcept {
    return basic_associative_view<false>{target_.as_const()};
  }
  [[nodiscard]] std::size_t size() const {
    auto v = readonly();
    return ops()->size(v.data_);
  }
  [[nodiscard]] result<std::vector<std::string>> keys() const {
    auto v = readonly();
    auto keys = ops()->keys(v.data_);
    for (const auto& key : keys)
      if (!valid_scalar(value{key}))
        return error::invalid_argument;
    return keys;
  }
  [[nodiscard]] result<target_type> find(std::string_view key) const {
    auto v = readonly();
    if constexpr (Mutable) {
      if (!ops()->find_mut)
        return error::unsupported;
      auto* p = ops()->find_mut(target_.mutable_data(), key);
      if (!p)
        return error::not_found;
      return instance::from_access(v.registry(), ops()->element(), p);
    } else {
      auto* p = ops()->find(v.data_, key);
      if (!p)
        return error::not_found;
      return const_instance::from_access(v.registry(), ops()->element(), p);
    }
  }
  [[nodiscard]] result<target_type> insert(std::string_view key) const
    requires(Mutable)
  {
    if (!valid_scalar(value{std::string(key)}))
      return error::invalid_argument;
    auto v = readonly();
    if (!ops()->insert)
      return error::unsupported;
    return instance::from_access(v.registry(), ops()->element(),
                                 ops()->insert(target_.mutable_data(), key));
  }
  [[nodiscard]] error erase(std::string_view key) const
    requires(Mutable)
  {
    if (!ops()->erase)
      return error::unsupported;
    return ops()->erase(target_.mutable_data(), key) ? error::success : error::not_found;
  }
  [[nodiscard]] error clear() const
    requires(Mutable)
  {
    if (!ops()->clear)
      return error::unsupported;
    ops()->clear(target_.mutable_data());
    return error::success;
  }
};
}
