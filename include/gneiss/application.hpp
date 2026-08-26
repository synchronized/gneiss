// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_HPP_
#define GNEISS_APPLICATION_HPP_

#include <gneiss/application.h>
#include <gneiss/core/result.hpp>

#include <cstdint>
#include <utility>

namespace gneiss {

/** 独占拥有 Application 的 RAII 包装；只允许在创建线程访问。 */
class application final {
public:
  application() noexcept = default;
  ~application() noexcept { reset(); }

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&& other) noexcept
      : handle_(std::exchange(other.handle_, GNEISS_NULL_APPLICATION)) {}
  application& operator=(application&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_APPLICATION);
    }
    return *this;
  }

  [[nodiscard]] static result create(const gneiss_application_desc& desc,
                                     application& out_application) noexcept {
    gneiss_application handle = GNEISS_NULL_APPLICATION;
    const auto native_result = gneiss_application_create(&desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_application.reset();
      out_application.handle_ = handle;
    }
    return from_native(native_result);
  }

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_APPLICATION; }
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }
  [[nodiscard]] result run(std::uint64_t max_frame_count = 0) noexcept {
    return from_native(gneiss_application_run(handle_, max_frame_count));
  }
  [[nodiscard]] result request_exit() noexcept {
    return from_native(gneiss_application_request_exit(handle_));
  }
  [[nodiscard]] result set_paused(bool is_paused) noexcept {
    return from_native(gneiss_application_set_paused(handle_, is_paused ? UINT8_C(1) : UINT8_C(0)));
  }
  [[nodiscard]] result get_world(gneiss_world& out_world) const noexcept {
    return from_native(gneiss_application_get_world(handle_, &out_world));
  }

  void reset() noexcept {
    if (handle_ != GNEISS_NULL_APPLICATION) {
      (void)gneiss_application_destroy(handle_);
      handle_ = GNEISS_NULL_APPLICATION;
    }
  }

private:
  gneiss_application handle_ = GNEISS_NULL_APPLICATION;
};

} // namespace gneiss

#endif
