// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_types.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/function/resource/resource_cache.hpp"
namespace gneiss::engine {
enum class texture_load_state : std::uint8_t { preparing, applied, failed, cancelled };
/** 请求中每项资源的结果；未生效项的 result 表示停止处理的原因，不表示旧内容失效。 */
struct asset_load_result {
  render_asset_reload source;
  engine::result result{engine::result::not_ready};
  bool applied{};
};
struct texture_load_completion {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  texture_load_state state{texture_load_state::failed};
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
  texture_load_state state{texture_load_state::preparing};
  std::size_t completed_assets{};
  std::size_t total_assets{};
  bool can_cancel{};
};

}
