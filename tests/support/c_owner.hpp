// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/values/core/result.hpp"
#include <exception>
#include <gneiss/engine/scene.h>
#include <gneiss/engine/world.h>
#include <utility>
namespace gneiss::test_support {
/** 测试用清理守卫；仅负责 C 句柄释放，不封装业务操作。 */
template <auto Destroy> class c_owner final {
public:
  c_owner() = default;
  ~c_owner() {
    if (reset().failed())
      std::terminate();
  }
  c_owner(c_owner&& other) noexcept : handle_(std::exchange(other.handle_, 0)) {}
  c_owner& operator=(c_owner&& other) noexcept {
    if (this != &other) {
      if (reset().failed())
        std::terminate();
      handle_ = std::exchange(other.handle_, 0);
    }
    return *this;
  }
  c_owner(const c_owner&) = delete;
  c_owner& operator=(const c_owner&) = delete;
  [[nodiscard]] std::uint64_t get() const noexcept { return handle_; }
  [[nodiscard]] std::uint64_t* put() noexcept { return &handle_; }
  result reset() noexcept {
    if (!handle_)
      return result::success;
    auto status = Destroy(handle_);
    if (status == GNEISS_SUCCESS)
      handle_ = 0;
    return result{status};
  }

private:
  std::uint64_t handle_{};
};
using world_owner = c_owner<gneiss_world_destroy>;
}
