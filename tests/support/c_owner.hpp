// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/values/core/result.hpp"
#include <exception>
#include <gneiss/engine/application.h>
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
/** 场景测试清理守卫；应用由夹具保证存活。 */
class scene_owner final {
public:
  scene_owner() = default;
  ~scene_owner() {
    if (reset().failed())
      std::terminate();
  }
  scene_owner(const scene_owner&) = delete;
  scene_owner& operator=(const scene_owner&) = delete;
  std::uint64_t get() const noexcept { return handle_; }
  std::uint64_t owner() const noexcept { return application_; }
  std::uint64_t* put(std::uint64_t application) noexcept {
    application_ = application;
    return &handle_;
  }
  result reset() noexcept {
    if (!handle_)
      return result::success;
    auto status = gneiss_scene_instance_unload(application_, handle_);
    if (status == GNEISS_SUCCESS)
      handle_ = 0;
    return result{status};
  }

private:
  std::uint64_t application_{};
  std::uint64_t handle_{};
};
using application_owner = c_owner<gneiss_application_destroy>;
using world_owner = c_owner<gneiss_world_destroy>;
}
