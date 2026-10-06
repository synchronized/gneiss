// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/texture_ktx2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::asset_internal {
class texture_payload_source;

/** 调用方提供的不可变设备能力快照；位顺序为 RGBA8 linear/sRGB、BC7 linear/sRGB。
 * generation 是不透明身份；零值保留整包兼容路径，不携带后端对象。 */
struct texture_prepare_profile {
  std::uint64_t generation{};
  std::array<bool, 4> sampled_transfer_formats{};
  bool operator==(const texture_prepare_profile&) const = default;
};

/** 准备阶段拥有的 CPU 数据；没有上传回执或已发布资源引用。 */
struct prepared_texture_data {
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t format;
  std::uint32_t color_space;
  std::vector<texture_mip> levels;
  std::vector<std::byte> manifest;
  std::vector<std::byte> payload;
  /** profile 非零时 payload 从选中变体起点开始，不含其他变体。 */
  texture_prepare_profile profile{};
  std::uint32_t selected_variant{UINT32_MAX};
  /** 选中文件变体的可重建来源；不包含 GPU 上传生命周期。 */
  std::shared_ptr<const texture_payload_source> payload_source{};
};

} // namespace gneiss::asset_internal
