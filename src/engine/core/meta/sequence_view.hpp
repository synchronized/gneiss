// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
namespace gneiss::engine::meta {
/** 借用视图；不延长目标寿命，结构修改后需重新获取元素视图。 */
template <bool Mutable> class basic_sequence_view {
  using target_type = std::conditional_t<Mutable, mutable_instance, instance>;
  friend class instance;
  friend class mutable_instance;
  explicit basic_sequence_view(target_type target) : target_(std::move(target)) {}
  instance readonly() const noexcept {
    if constexpr (Mutable)
      return target_.as_const();
    else
      return target_;
  }
  const sequence_ops* ops() const noexcept {
    return readonly().access_->template get<sequence_ops>();
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
      return mutable_instance::from_access(v.registry_, ops()->element(),
                                           ops()->at_mut(target_.data_, index));
    } else
      return instance::from_access(v.registry_, ops()->element(), ops()->at(v.data_, index));
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  [[nodiscard]] bool is_resizable() const noexcept { return ops()->resize != nullptr; }
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
