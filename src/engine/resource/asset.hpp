// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_ref.hpp"
namespace gneiss {
/** 逻辑资产身份；内容就绪状态由加载请求报告，非空引用不代表加载成功。
 * 共享既有 ref 控制块，析构线程策略仍由创建者指定。 */
class asset {
public:
  virtual ~asset() = default;
  asset(const asset&) = delete;
  asset& operator=(const asset&) = delete;
  [[nodiscard]] const asset_identity& source() const noexcept { return identity_; }
  [[nodiscard]] type_id type() const noexcept { return type_; }

protected:
  explicit asset(type_id type) noexcept : type_(type) {}
  mutable asset_identity identity_;

private:
  type_id type_;
};
/** 校验进程类型身份后借用同一控制块，不产生新的资源所有者。 */
template <class T>
  requires std::derived_from<T, asset>
[[nodiscard]] ref<const T> asset_cast(const ref<const asset>& value) noexcept {
  if constexpr (std::is_same_v<T, asset>)
    return value;
  return value && value->type() == runtime_type_of<T>()
             ? value.alias(static_cast<const T*>(value.get()))
             : ref<const T>{};
}
}
