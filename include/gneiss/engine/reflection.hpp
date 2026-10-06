// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_HPP_
#define GNEISS_REFLECTION_HPP_

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/reflection.h>

#include <cstdint>
#include <exception>

namespace gneiss {

/** Type Registry 的独占 RAII 包装。查询结果仍由 Registry 持有；并发访问需外部同步。
 * 析构或移动覆盖若关闭失败则终止进程；需处理错误时先显式 reset()。 */
class type_registry final {
public:
  type_registry() noexcept = default;
  ~type_registry() noexcept { reset_or_terminate(); }

  type_registry(const type_registry&) = delete;
  type_registry& operator=(const type_registry&) = delete;

  type_registry(type_registry&& other) noexcept : handle_(other.release()) {}
  type_registry& operator=(type_registry&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      handle_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] static result create(type_registry& output) noexcept {
    gneiss_type_registry handle = GNEISS_NULL_TYPE_REGISTRY;
    const auto status = from_native(gneiss_type_registry_create(&handle));
    if (status.ok()) {
      type_registry candidate;
      candidate.handle_ = handle;
      const auto closed = output.reset();
      if (closed.failed()) {
        return closed;
      }
      output.handle_ = candidate.release();
    }
    return status;
  }

  [[nodiscard]] result register_type(const gneiss_type_desc& desc) noexcept {
    return from_native(gneiss_type_registry_register(handle_, &desc));
  }
  [[nodiscard]] result bind_property(gneiss_type_id type_id, gneiss_field_id field_id,
                                     const gneiss_property_accessor_desc& desc) noexcept {
    return from_native(gneiss_type_registry_bind_property(handle_, type_id, field_id, &desc));
  }
  [[nodiscard]] result freeze() noexcept {
    return from_native(gneiss_type_registry_freeze(handle_));
  }
  /** 查询是否冻结；失败时不改变输出。 */
  [[nodiscard]] result is_frozen(bool& output) const noexcept {
    std::uint8_t value{};
    const auto status = from_native(gneiss_type_registry_is_frozen(handle_, &value));
    if (status.ok()) {
      output = value != 0U;
    }
    return status;
  }
  /** 按序号借用冻结 Registry 元数据，有效期不超过 Registry。 */
  [[nodiscard]] result type_at(std::uint32_t index, gneiss_type_info& output) const noexcept {
    output = GNEISS_TYPE_INFO_INIT;
    return from_native(gneiss_type_registry_type_at(handle_, index, &output));
  }
  [[nodiscard]] result type_count(std::uint32_t& output) const noexcept {
    return from_native(gneiss_type_registry_type_count(handle_, &output));
  }
  [[nodiscard]] result find_type(gneiss_type_id id, gneiss_type_info& output) const noexcept {
    output = GNEISS_TYPE_INFO_INIT;
    return from_native(gneiss_type_registry_find_type(handle_, id, &output));
  }
  [[nodiscard]] result find_field(gneiss_type_id type_id, gneiss_field_id field_id,
                                  gneiss_field_info& output) const noexcept {
    output = GNEISS_FIELD_INFO_INIT;
    return from_native(gneiss_type_registry_find_field(handle_, type_id, field_id, &output));
  }
  [[nodiscard]] result get_property(gneiss_type_id type_id, gneiss_field_id field_id,
                                    gneiss_property_target target,
                                    gneiss_property_value& output) const noexcept {
    return from_native(
        gneiss_type_registry_get_property(handle_, type_id, field_id, target, &output));
  }
  [[nodiscard]] result set_property(gneiss_type_id type_id, gneiss_field_id field_id,
                                    gneiss_property_target target,
                                    const gneiss_property_value& value) const noexcept {
    return from_native(
        gneiss_type_registry_set_property(handle_, type_id, field_id, target, &value));
  }

  [[nodiscard]] gneiss_type_registry get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != GNEISS_NULL_TYPE_REGISTRY;
  }

  /** 幂等关闭；无效句柄视为已释放，其他失败保留句柄供所属线程重试。
   * 返回结果可供检查；保留直接 reset() 的既有调用方式。 */
  result reset() noexcept {
    if (handle_ == GNEISS_NULL_TYPE_REGISTRY) {
      return result::success;
    }
    const auto status = from_native(gneiss_type_registry_destroy(handle_));
    if (status.failed() && status != result::invalid_handle) {
      return status;
    }
    handle_ = GNEISS_NULL_TYPE_REGISTRY;
    return result::success;
  }

  /** 转移原始 Registry 所有权，调用方负责销毁；借用元数据寿命不变。 */
  [[nodiscard]] gneiss_type_registry release() noexcept {
    const auto value = handle_;
    handle_ = GNEISS_NULL_TYPE_REGISTRY;
    return value;
  }

private:
  void reset_or_terminate() noexcept {
    if (reset().failed()) {
      std::terminate();
    }
  }
  gneiss_type_registry handle_ = GNEISS_NULL_TYPE_REGISTRY;
};

} // namespace gneiss

#endif
