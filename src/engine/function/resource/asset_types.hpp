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

enum class asset_load_state : std::uint8_t { preparing, applied, failed, cancelled };
/** 请求中每项资源的结果；未生效项的 result 表示停止处理的原因，不表示旧内容失效。 */
struct asset_load_result {
  asset_source source;
  engine::result result{engine::result::not_ready};
  bool applied{};
};
struct asset_load_completion {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  asset_load_state state{asset_load_state::failed};
  engine::result result{engine::result::internal};
  std::string message{};
  /** 解析错误位置；文件读取失败时 path 为请求 URI。 */
  std::string path;
  std::size_t byte_offset{};
  std::vector<render_asset_ref> assets;
  /** 包含显式请求及已发现依赖；取消/失败也保留已成功项，元数据不延长资源寿命。 */
  std::vector<asset_load_result> results;
  /** 本次准备使用的设备能力。 */
  texture_prepare_profile profile{};
  double prepare_ms{};
  double queue_ms{};
  double commit_ms{};
  /** 本请求累计成功解码的字节估算，不是同时驻留内存。 */
  std::size_t prepared_bytes{};
};
struct asset_load_progress {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  asset_load_state state{asset_load_state::preparing};
  std::size_t completed_assets{};
  std::size_t total_assets{};
  bool can_cancel{};
};

} // namespace gneiss::engine
