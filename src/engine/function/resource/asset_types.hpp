// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_preparation.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/resource_cache.hpp"
namespace gneiss::engine {
struct material_data;
/** 统一持有资产缓存条目；非空仅表示身份存在，不代表 GPU 就绪。 */
using render_asset_ref = asset_ref<const resource_cache::entry>;
/** 混合加载结果转为类型化引用；空值和类型不符均返回空。 */
template <typename T>
[[nodiscard]] asset_ref<const T> asset_cast(const render_asset_ref& asset) noexcept {
  return asset ? asset->as<T>() : asset_ref<const T>{};
}
[[nodiscard]] rid asset_rid(const render_asset_ref& asset) noexcept;
[[nodiscard]] GNEISS_API render_asset_type asset_kind(const render_asset_ref& asset) noexcept;
[[nodiscard]] inline std::string_view asset_uri(const render_asset_ref& asset) noexcept {
  return asset ? std::string_view(asset->uri) : std::string_view{};
}

inline std::string_view asset_uri(const mesh& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const mesh& value) noexcept { return resource_handle(value); }
inline render_asset_type asset_kind(const mesh& value) noexcept {
  return value ? render_asset_type::mesh : render_asset_type::invalid;
}
inline std::string_view asset_uri(const texture& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const texture& value) noexcept { return resource_handle(value); }
inline render_asset_type asset_kind(const texture& value) noexcept {
  return value ? render_asset_type::texture : render_asset_type::invalid;
}
inline std::string_view asset_uri(const material& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const material& value) noexcept { return resource_handle(value); }
inline render_asset_type asset_kind(const material& value) noexcept {
  return value ? render_asset_type::material : render_asset_type::invalid;
}

/** 更新任务只携带不可变 CPU 数据；后端所有权留在渲染服务。 */
struct render_resource_content {
  std::shared_ptr<const mesh_data> mesh;
  std::shared_ptr<const material_data> material;
  std::shared_ptr<const texture_data> texture;
  std::size_t bytes{};
};

struct render_asset_update {
  render_asset_ref target;
  std::uint64_t sequence{};
  render_resource_content data;
  std::array<texture, 5> dependencies;
};
}
