// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_HPP_
#define GNEISS_WORLD_HPP_

#include <gneiss/core/entity.hpp>
#include <gneiss/core/result.hpp>
#include <gneiss/world.h>

#include <utility>

namespace gneiss {

/** 独占拥有一个 World 的 RAII 包装；只允许在创建线程访问。 */
class world final {
public:
  world() noexcept = default;
  ~world() noexcept { reset(); }

  world(const world&) = delete;
  world& operator=(const world&) = delete;

  world(world&& other) noexcept : handle_(std::exchange(other.handle_, GNEISS_NULL_WORLD)) {}
  world& operator=(world&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_WORLD);
    }
    return *this;
  }

  [[nodiscard]] static result create(world& out_world) noexcept {
    const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
    gneiss_world handle = GNEISS_NULL_WORLD;
    const auto native_result = gneiss_world_create(&desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_world.reset();
      out_world.handle_ = handle;
    }
    return from_native(native_result);
  }

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_WORLD; }
  [[nodiscard]] gneiss_world get() const noexcept { return handle_; }

  [[nodiscard]] result create_entity(entity_id& out_entity) noexcept {
    gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
    const auto native_result = gneiss_world_entity_create(handle_, &native_entity);
    if (native_result == GNEISS_SUCCESS) {
      out_entity = entity_id{native_entity};
    }
    return from_native(native_result);
  }

  [[nodiscard]] result destroy_entity(entity_id entity) noexcept {
    return from_native(gneiss_world_entity_destroy(handle_, entity.get()));
  }

  [[nodiscard]] result is_alive(entity_id entity, bool& out_is_alive) const noexcept {
    uint8_t native_is_alive = 0;
    const auto native_result =
        gneiss_world_entity_is_alive(handle_, entity.get(), &native_is_alive);
    if (native_result == GNEISS_SUCCESS) {
      out_is_alive = native_is_alive != 0;
    }
    return from_native(native_result);
  }

  void reset() noexcept {
    if (handle_ != GNEISS_NULL_WORLD) {
      (void)gneiss_world_destroy(handle_);
      handle_ = GNEISS_NULL_WORLD;
    }
  }

private:
  gneiss_world handle_ = GNEISS_NULL_WORLD;
};

} // namespace gneiss

#endif
