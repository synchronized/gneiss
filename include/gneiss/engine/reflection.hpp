// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_HPP_
#define GNEISS_REFLECTION_HPP_

#include <gneiss/engine/detail/reflection_values.hpp>
#include <gneiss/engine/reflection.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gneiss {

namespace detail {
struct property_binding {
  type_id type;
  field_id field;
  property_accessor_desc callbacks;
};
} // namespace detail

namespace sdk {

class released_type_registry;

/** Type Registry 的独占 RAII 包装。查询名称借用 Registry；冻结前修改及关闭需外部同步。
 * 析构或移动覆盖若关闭失败则终止进程；需处理错误时先显式 reset()。 */
class type_registry final {
public:
  type_registry() noexcept = default;
  ~type_registry() noexcept { reset_or_terminate(); }

  type_registry(const type_registry&) = delete;
  type_registry& operator=(const type_registry&) = delete;

  type_registry(type_registry&& other) noexcept
      : handle_(std::exchange(other.handle_, 0)), bindings_(std::move(other.bindings_)) {}
  type_registry& operator=(type_registry&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      handle_ = std::exchange(other.handle_, 0);
      bindings_ = std::move(other.bindings_);
    }
    return *this;
  }

  [[nodiscard]] static result create(type_registry& output) noexcept;

  /** 同步复制类型与字段描述；失败不产生包装层的半注册状态。 */
  [[nodiscard]] result register_type(const type_desc& desc) const noexcept;
  /** 冻结前串行调用；描述被复制，user_data 仍由调用方持有至关闭。 */
  [[nodiscard]] result bind_property(type_id type, field_id field,
                                     const property_accessor_desc& desc) noexcept;
  /** 字段数组由输出拥有；名称借用 Registry，失败保持输出。 */
  [[nodiscard]] result type_at(std::uint32_t index, type_info& output) const noexcept;
  [[nodiscard]] result find_type(type_id id, type_info& output) const noexcept;
  [[nodiscard]] result find_field(type_id type, field_id field, field_info& output) const noexcept;
  [[nodiscard]] result get_property(type_id type, field_id field, property_target target,
                                    property_value& output) const noexcept;
  [[nodiscard]] result set_property(type_id type, field_id field, property_target target,
                                    const property_value& value) const noexcept;
  /** 显式 C ABI 互操作，描述和回调上下文保持原 C 契约。 */
  [[nodiscard]] result register_type_native(const gneiss_type_desc& desc) const noexcept;
  [[nodiscard]] result
  bind_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                       const gneiss_property_accessor_desc& desc) const noexcept;
  [[nodiscard]] result freeze() const noexcept;
  /** 查询是否冻结；失败时不改变输出。 */
  [[nodiscard]] result is_frozen(bool& output) const noexcept;
  /** 按序号借用冻结 Registry 元数据，有效期不超过 Registry。 */
  [[nodiscard]] result type_at_native(std::uint32_t index, gneiss_type_info& output) const noexcept;
  [[nodiscard]] result type_count(std::uint32_t& output) const noexcept;
  [[nodiscard]] result find_type_native(gneiss_type_id id, gneiss_type_info& output) const noexcept;
  [[nodiscard]] result find_field_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                         gneiss_field_info& output) const noexcept;
  [[nodiscard]] result get_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                           gneiss_property_target target,
                                           gneiss_property_value& output) const noexcept;
  [[nodiscard]] result set_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                           gneiss_property_target target,
                                           const gneiss_property_value& value) const noexcept;

  [[nodiscard]] gneiss_type_registry get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != GNEISS_NULL_TYPE_REGISTRY;
  }

  /** 幂等关闭；无效句柄视为已释放，其他失败保留句柄；销毁与读写由调用方外部同步。
   * 返回结果可供检查；保留直接 reset() 的既有调用方式。 */
  result reset() noexcept;

  /** 同时转移 Registry 和稳定回调存储；载体析构关闭 Registry。 */
  [[nodiscard]] released_type_registry release() noexcept;
  /** 关闭原拥有者后接管载体；关闭失败保留两者。 */
  [[nodiscard]] static result adopt(released_type_registry&& source,
                                    type_registry& output) noexcept;

private:
  void reset_or_terminate() noexcept;
  gneiss_type_registry handle_ = GNEISS_NULL_TYPE_REGISTRY;
  std::vector<std::unique_ptr<gneiss::detail::property_binding>> bindings_;
};

/** 独占释放载体，不隐式转为 C 句柄；get 只借用，禁止提前销毁仍在使用的 Registry。 */
class released_type_registry final {
public:
  released_type_registry() noexcept = default;
  released_type_registry(const released_type_registry&) = delete;
  released_type_registry& operator=(const released_type_registry&) = delete;
  released_type_registry(released_type_registry&&) noexcept = default;
  released_type_registry& operator=(released_type_registry&&) noexcept = default;
  [[nodiscard]] gneiss_type_registry get() const noexcept { return owner_.get(); }
  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(owner_); }
  result reset() noexcept;

private:
  friend class type_registry;
  explicit released_type_registry(type_registry&& owner) noexcept : owner_(std::move(owner)) {}
  type_registry owner_;
};

} // namespace sdk
} // namespace gneiss

#include <gneiss/engine/detail/reflection.inl>

#endif
