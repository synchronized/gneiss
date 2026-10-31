// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/resource_cache.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <concepts>
namespace gneiss::engine {
enum class render_resource_kind : std::uint32_t {
  invalid = 0U,
  mesh = 1U,
  material = 2U,
  texture = 3U
};
struct render_resource_source final {
  std::string uri;
  render_resource_kind type = render_resource_kind::mesh;
};
/** 统一持有资产缓存条目；非空仅表示身份存在，不代表 GPU 就绪。 */
using render_resource_ref = asset_ref<const resource_cache::entry>;
/** 混合加载结果转为类型化引用；空值和类型不符均返回空。 */
template <typename T>
[[nodiscard]] asset_ref<const T> asset_cast(const render_resource_ref& asset) noexcept {
  return asset ? asset->as<T>() : asset_ref<const T>{};
}
[[nodiscard]] GNEISS_API rid asset_rid(const render_resource_ref& asset) noexcept;
[[nodiscard]] GNEISS_API render_resource_kind asset_kind(const render_resource_ref& asset) noexcept;
[[nodiscard]] inline std::string_view asset_uri(const render_resource_ref& asset) noexcept {
  return asset ? std::string_view(asset->uri) : std::string_view{};
}

/** 资源分类属于加载层，不让资源对象反向依赖 render_resource_kind。 */
template <typename T> struct resource_traits {};
template <> struct resource_traits<mesh_resource> {
  static constexpr render_resource_kind kind = render_resource_kind::mesh;
};
template <> struct resource_traits<texture_resource> {
  static constexpr render_resource_kind kind = render_resource_kind::texture;
};
template <> struct resource_traits<material_resource> {
  static constexpr render_resource_kind kind = render_resource_kind::material;
};
template <typename T>
concept render_resource = requires {
  { resource_traits<T>::kind } -> std::convertible_to<render_resource_kind>;
};

template <render_resource T>
[[nodiscard]] inline std::string_view asset_uri(const asset_ref<const T>& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
template <render_resource T>
[[nodiscard]] inline rid asset_rid(const asset_ref<const T>& value) noexcept {
  return resource_handle(value);
}
template <render_resource T>
[[nodiscard]] inline render_resource_kind asset_kind(const asset_ref<const T>& value) noexcept {
  return value ? resource_traits<T>::kind : render_resource_kind::invalid;
}

} // namespace gneiss::engine
