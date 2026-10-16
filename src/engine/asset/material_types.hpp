// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>

namespace gneiss::asset_internal {
/** 材质纯值语义；资产与渲染共用，不持有资源或后端对象。 */
enum class material_alpha_mode : std::uint32_t { opaque, mask, blend };
enum class texture_filter : std::uint32_t { nearest, linear };
enum class texture_mip_filter : std::uint32_t { none, nearest, linear };
enum class texture_address : std::uint32_t { repeat, clamp, mirror };
struct texture_sampling {
  std::uint32_t uv_set{};
  texture_filter mag_filter{texture_filter::linear};
  texture_filter min_filter{texture_filter::linear};
  texture_mip_filter mip_filter{texture_mip_filter::linear};
  texture_address address_u{texture_address::repeat};
  texture_address address_v{texture_address::repeat};
};
/** 拒绝由不可信输入转换出的未知枚举值。 */
[[nodiscard]] constexpr bool valid_sampling(const texture_sampling& value) noexcept {
  return value.uv_set <= 1U && value.mag_filter <= texture_filter::linear &&
         value.min_filter <= texture_filter::linear &&
         value.mip_filter <= texture_mip_filter::linear &&
         value.address_u <= texture_address::mirror && value.address_v <= texture_address::mirror;
}
} // namespace gneiss::asset_internal
