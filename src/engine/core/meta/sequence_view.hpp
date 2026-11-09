// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include <unordered_set>
namespace gneiss::engine::meta {
/** 借用视图；不延长目标寿命，结构修改后需重新获取元素视图。 */
template <bool Mutable> class basic_sequence_view {
  using target_type = std::conditional_t<Mutable, instance, const_instance>;
  friend class const_instance;
  friend class instance;
  explicit basic_sequence_view(target_type target) : target_(std::move(target)) {}
  const_instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const sequence_ops* ops() const noexcept {
    return readonly().access()->template get<sequence_ops>();
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
  [[nodiscard]] result<target_type> at(std::size_t index) const noexcept try {
    auto v = readonly();
    auto count = size();
    if (!count)
      return count.error();
    if (index >= *count)
      return error::not_found;
    if constexpr (Mutable) {
      if (!ops()->at_mut)
        return error::unsupported;
      return instance::from_access(v.registry(), ops()->element(),
                                   ops()->at_mut(target_.data_, index));
    } else
      return const_instance::from_access(v.registry(), ops()->element(), ops()->at(v.data_, index));
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] bool is_fixed_size() const noexcept { return ops()->fixed_size; }
  [[nodiscard]] bool has_identity() const noexcept { return ops()->identity != nullptr; }
  [[nodiscard]] result<guid> identity(std::size_t index) const noexcept try {
    if (!has_identity())
      return error::unsupported;
    auto count = size();
    if (!count)
      return count.error();
    if (index >= *count)
      return error::not_found;
    auto id = ops()->identity(readonly().data_, index);
    return id ? result<guid>{id} : result<guid>{error::invalid_argument};
  } catch (...) {
    return error::internal;
  }
  /** 拒绝空身份或重号；查找失败不回退下标。 */
  [[nodiscard]] result<target_type> find(guid id) const noexcept try {
    if (!id)
      return error::invalid_argument;
    if (!has_identity())
      return error::unsupported;
    auto count = size();
    if (!count)
      return count.error();
    std::unordered_set<guid, guid_hash> seen;
    std::size_t found = *count;
    for (std::size_t i = 0; i < *count; ++i) {
      auto current = identity(i);
      if (!current)
        return current.error();
      if (!seen.insert(*current).second)
        return error::invalid_argument;
      if (*current == id)
        found = i;
    }
    return found == *count ? result<target_type>{error::not_found} : at(found);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] bool is_resizable() const noexcept { return ops()->resize != nullptr; }
  [[nodiscard]] bool supports_replacement() const noexcept { return ops()->replace != nullptr; }
  /** 在临时序列上初始化，再通过容器约束校验并替换；不开放原集合可写元素。 */
  [[nodiscard]] error
  replace(const std::function<error(const instance&)>& initialize) const noexcept
    requires(Mutable)
  try {
    if (!ops()->replace)
      return error::unsupported;
    if (!initialize)
      return error::invalid_argument;
    return ops()->replace(target_.data_, [&](void* data, const detail::type_ops& access) {
      auto candidate = instance::from_access(readonly().registry(), access, data);
      return candidate ? initialize(*candidate) : candidate.error();
    });
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] error resize(std::size_t count) const noexcept
    requires(Mutable)
  try {
    auto v = readonly();
    if (!ops()->resize)
      return count == ops()->size(v.data_)
                 ? error::success
                 : (ops()->fixed_size ? error::invalid_argument : error::unsupported);
    ops()->resize(target_.data_, count);
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (const std::length_error&) {
    return error::invalid_argument;
  } catch (...) {
    return error::internal;
  }
};
}
