// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_HPP_
#define GNEISS_REFLECTION_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/reflection.h>

#include <cstdint>

namespace gneiss {

/** Type Registry 的独占 RAII 包装。查询结果仍由 Registry 持有。 */
class type_registry final {
public:
  type_registry() noexcept = default;
  ~type_registry() noexcept { reset(); }

  type_registry(const type_registry&) = delete;
  type_registry& operator=(const type_registry&) = delete;

  type_registry(type_registry&& other) noexcept : handle_(other.release()) {}
  type_registry& operator=(type_registry&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] static result create(type_registry& output) noexcept {
    gneiss_type_registry handle = GNEISS_NULL_TYPE_REGISTRY;
    const auto status = from_native(gneiss_type_registry_create(&handle));
    if (status.ok()) {
      output.reset();
      output.handle_ = handle;
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

  void reset() noexcept {
    if (handle_ != GNEISS_NULL_TYPE_REGISTRY) {
      (void)gneiss_type_registry_destroy(handle_);
      handle_ = GNEISS_NULL_TYPE_REGISTRY;
    }
  }

private:
  [[nodiscard]] gneiss_type_registry release() noexcept {
    const auto value = handle_;
    handle_ = GNEISS_NULL_TYPE_REGISTRY;
    return value;
  }

  gneiss_type_registry handle_ = GNEISS_NULL_TYPE_REGISTRY;
};

} // namespace gneiss

#endif
