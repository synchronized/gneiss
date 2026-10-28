// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/sha256.hpp"
#include "engine/function/resource/asset_types.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::engine {
/** 准备实际读取的输入；identity.type 使用所属资产类型，区分描述与负载的 URI。
 * 记录本次实际读取内容的摘要与长度；不是包含依赖及准备配置的完整缓存版本。 */
struct prepared_source final {
  asset_identity identity;
  std::uint64_t bytes{};
  sha256_digest digest{};
};

/** 后台准备结果；材质保留描述引用，纹理依赖使用 URI，不持有后端对象。 */
struct prepared_asset {
  asset_ref<const material_description> description;
  asset_source source;
  /** 描述在首位，纹理负载在其后；材质的资产依赖仍由 texture_uris 表示。 */
  std::vector<prepared_source> sources;
  mesh_data mesh;
  texture_data texture{};
  material_parameters material{};
  std::array<std::string, 5> texture_uris;
  std::size_t bytes{};
};
/** 已读输入的纯 CPU 解码；不执行文件 I/O。成功才替换输出。
 * Texture 传入描述字节、负载 URI 和负载；Mesh 只使用描述字节。
 * 选中纹理变体保留 CPU 数据，恢复上传无需读文件。 */
[[nodiscard]] result decode_loaded_asset(const asset_source& source,
                                         std::vector<std::byte> description,
                                         std::string payload_uri, std::vector<std::byte> payload,
                                         prepared_asset& output, asset_diagnostic& diagnostic,
                                         std::size_t maximum_bytes, texture_prepare_profile profile,
                                         asset_ref<const material_description> material = {},
                                         std::size_t material_bytes = 0U);

struct prepared_batch {
  std::vector<prepared_asset> assets;
  std::size_t bytes{};
  /** 整文件快照持有的输入字节；区间读取没有源副本，不计入此值。 */
  std::size_t input_bytes{};
};
} // namespace gneiss::engine
