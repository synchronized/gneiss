// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/resource_cache.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/texture/texture_data.hpp"
namespace gneiss::engine {
enum class asset_type : std::uint32_t { invalid = 0U, mesh = 1U, material = 2U, texture = 3U };
struct asset_source final {
  std::string uri;
  asset_type type = asset_type::mesh;
};
/** 统一持有资产缓存条目；非空仅表示身份存在，不代表 GPU 就绪。 */
using render_asset_ref = asset_ref<const resource_cache::entry>;
/** 混合加载结果转为类型化引用；空值和类型不符均返回空。 */
template <typename T>
[[nodiscard]] asset_ref<const T> asset_cast(const render_asset_ref& asset) noexcept {
  return asset ? asset->as<T>() : asset_ref<const T>{};
}
[[nodiscard]] GNEISS_API rid asset_rid(const render_asset_ref& asset) noexcept;
[[nodiscard]] GNEISS_API asset_type asset_kind(const render_asset_ref& asset) noexcept;
[[nodiscard]] inline std::string_view asset_uri(const render_asset_ref& asset) noexcept {
  return asset ? std::string_view(asset->uri) : std::string_view{};
}

inline std::string_view asset_uri(const mesh& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const mesh& value) noexcept { return resource_handle(value); }
inline asset_type asset_kind(const mesh& value) noexcept {
  return value ? asset_type::mesh : asset_type::invalid;
}
inline std::string_view asset_uri(const texture& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const texture& value) noexcept { return resource_handle(value); }
inline asset_type asset_kind(const texture& value) noexcept {
  return value ? asset_type::texture : asset_type::invalid;
}
inline std::string_view asset_uri(const material& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const material& value) noexcept { return resource_handle(value); }
inline asset_type asset_kind(const material& value) noexcept {
  return value ? asset_type::material : asset_type::invalid;
}

} // namespace gneiss::engine
