// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
namespace gneiss::engine::meta {
/** 借用视图；不延长目标寿命，结构修改后需重新获取元素视图。 */
template <bool Mutable> class basic_associative_view {
  using target_type = std::conditional_t<Mutable, mutable_instance, instance>;
  friend class instance;
  friend class mutable_instance;
  explicit basic_associative_view(target_type target) : target_(std::move(target)) {}
  instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const associative_ops* ops() const noexcept {
    return readonly().access_->template get<associative_ops>();
  }
  target_type target_;

public:
  [[nodiscard]] result<std::size_t> size() const noexcept try {
    auto v = readonly();
    return ops()->size(v.data_);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<std::vector<std::string>> keys() const noexcept try {
    auto v = readonly();
    auto keys = ops()->keys(v.data_);
    for (const auto& key : keys)
      if (!valid_scalar(value{key}))
        return error::invalid_argument;
    return keys;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> find(std::string_view key) const noexcept try {
    auto v = readonly();
    if constexpr (Mutable) {
      if (!ops()->find_mut)
        return error::unsupported;
      auto* p = ops()->find_mut(target_.data_, key);
      if (!p)
        return error::not_found;
      return mutable_instance::from_access(v.registry_, ops()->element(), p);
    } else {
      auto* p = ops()->find(v.data_, key);
      if (!p)
        return error::not_found;
      return instance::from_access(v.registry_, ops()->element(), p);
    }
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] result<target_type> insert(std::string_view key) const noexcept
    requires(Mutable)
  try {
    if (!valid_scalar(value{std::string(key)}))
      return error::invalid_argument;
    auto v = readonly();
    if (!ops()->insert)
      return error::unsupported;
    return mutable_instance::from_access(v.registry_, ops()->element(),
                                         ops()->insert(target_.data_, key));
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error erase(std::string_view key) const noexcept
    requires(Mutable)
  try {
    if (!ops()->erase)
      return error::unsupported;
    return ops()->erase(target_.data_, key) ? error::success : error::not_found;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error clear() const noexcept
    requires(Mutable)
  try {
    if (!ops()->clear)
      return error::unsupported;
    ops()->clear(target_.data_);
    return error::success;
  } catch (...) {
    return error::internal;
  }
};
}
