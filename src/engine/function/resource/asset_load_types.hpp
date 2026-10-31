// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/sha256.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <string>
#include <vector>
namespace gneiss::engine {
class asset_manager;
/** 本次实际读取的来源快照，不是缓存版本或事务标识。 */
struct asset_data_source {
  asset_identity identity;
  std::uint64_t bytes{};
  sha256_digest digest{};
};
struct asset_data_metrics {
  std::vector<asset_data_source> sources;
  std::size_t bytes{};
  double decode_ms{};
  double queue_ms{};
};
struct asset_load_options {
  bool reload = false;
  texture_prepare_profile texture_profile{};
};
/** 完整参数编码用于比较身份，不使用可能冲突的摘要。默认对象加载保持空变体键。 */
inline std::string asset_load_parameters(const asset_load_options& options) {
  if (options.texture_profile == texture_prepare_profile{})
    return {};
  auto key = std::to_string(options.texture_profile.generation);
  for (bool format : options.texture_profile.sampled_transfer_formats)
    key += format ? ":1" : ":0";
  return key;
}
}
