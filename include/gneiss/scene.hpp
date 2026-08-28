// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_HPP_
#define GNEISS_SCENE_HPP_

#include <gneiss/core/entity.hpp>
#include <gneiss/core/result.hpp>
#include <gneiss/scene.h>

#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace gneiss {

/** 不拥有 Scene Node 的强类型运行时标识。 */
class scene_node_id final {
public:
  constexpr scene_node_id() noexcept = default;
  explicit constexpr scene_node_id(gneiss_scene_node_id value) noexcept : value_(value) {}
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return value_ != GNEISS_NULL_SCENE_NODE_ID;
  }
  [[nodiscard]] constexpr gneiss_scene_node_id get() const noexcept { return value_; }
  friend constexpr bool operator==(scene_node_id, scene_node_id) noexcept = default;

private:
  gneiss_scene_node_id value_ = GNEISS_NULL_SCENE_NODE_ID;
};

inline constexpr scene_node_id null_scene_node_id{};
using transform = gneiss_transform;
using scene_instance_node_info = gneiss_scene_instance_node_info;
using scene_mesh_renderer_desc = gneiss_scene_mesh_renderer_desc;
using scene_mesh_renderer_node_desc = gneiss_scene_mesh_renderer_node_desc;

/** 独占拥有已加载场景；必须在所属 Application 销毁前释放。 */
class scene_instance final {
public:
  scene_instance() noexcept = default;
  ~scene_instance() noexcept { reset(); }

  scene_instance(const scene_instance&) = delete;
  scene_instance& operator=(const scene_instance&) = delete;
  scene_instance(scene_instance&& other) noexcept
      : application_(std::exchange(other.application_, GNEISS_NULL_APPLICATION)),
        handle_(std::exchange(other.handle_, GNEISS_NULL_SCENE_INSTANCE)) {}
  scene_instance& operator=(scene_instance&& other) noexcept {
    if (this != &other) {
      reset();
      application_ = std::exchange(other.application_, GNEISS_NULL_APPLICATION);
      handle_ = std::exchange(other.handle_, GNEISS_NULL_SCENE_INSTANCE);
    }
    return *this;
  }

  [[nodiscard]] static result load(gneiss_application application, std::string_view uri,
                                   scene_instance& out_instance) noexcept {
    gneiss_scene_instance handle = GNEISS_NULL_SCENE_INSTANCE;
    const auto native_result =
        gneiss_scene_instance_load(application, uri.data(), uri.size(), &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_instance.reset();
      out_instance.application_ = application;
      out_instance.handle_ = handle;
    }
    return from_native(native_result);
  }

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_SCENE_INSTANCE; }
  [[nodiscard]] gneiss_scene_instance get() const noexcept { return handle_; }
  [[nodiscard]] result find_node(std::string_view uuid, scene_node_id& out_node) const noexcept {
    gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
    const auto native_result =
        gneiss_scene_instance_find_node(application_, handle_, uuid.data(), uuid.size(), &node);
    if (native_result == GNEISS_SUCCESS) {
      out_node = scene_node_id{node};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result get_node_count(std::uint64_t& out_count) const noexcept {
    return from_native(gneiss_scene_instance_get_node_count(application_, handle_, &out_count));
  }
  [[nodiscard]] result get_node_info(std::uint64_t index,
                                     scene_instance_node_info& out_info) const noexcept {
    return from_native(
        gneiss_scene_instance_get_node_info(application_, handle_, index, &out_info));
  }
  [[nodiscard]] result create_mesh_renderer_node(const scene_mesh_renderer_node_desc& desc,
                                                 scene_node_id& out_node) noexcept {
    gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
    const auto native_result =
        gneiss_scene_instance_create_mesh_renderer_node(application_, handle_, &desc, &node);
    if (native_result == GNEISS_SUCCESS) {
      out_node = scene_node_id{node};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result set_mesh_renderer(scene_node_id node,
                                         const scene_mesh_renderer_desc& desc) noexcept {
    return from_native(
        gneiss_scene_instance_set_mesh_renderer(application_, handle_, node.get(), &desc));
  }
  [[nodiscard]] result serialize(std::string& out_json) const noexcept {
    std::uint64_t length = 0;
    auto native_result =
        gneiss_scene_instance_serialize(application_, handle_, nullptr, 0U, &length);
    if (native_result != GNEISS_SUCCESS) {
      return from_native(native_result);
    }
    if (length > std::numeric_limits<std::size_t>::max()) {
      return result::out_of_memory;
    }
    try {
      std::string value(static_cast<std::size_t>(length), '\0');
      native_result = gneiss_scene_instance_serialize(application_, handle_, value.data(),
                                                      value.size(), &length);
      if (native_result == GNEISS_SUCCESS) {
        out_json = std::move(value);
      }
      return from_native(native_result);
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }
  void reset() noexcept {
    if (handle_ != GNEISS_NULL_SCENE_INSTANCE) {
      (void)gneiss_scene_instance_unload(application_, handle_);
      handle_ = GNEISS_NULL_SCENE_INSTANCE;
      application_ = GNEISS_NULL_APPLICATION;
    }
  }

private:
  gneiss_application application_ = GNEISS_NULL_APPLICATION;
  gneiss_scene_instance handle_ = GNEISS_NULL_SCENE_INSTANCE;
};

} // namespace gneiss

#endif
