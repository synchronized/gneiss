// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>
#include <string>

namespace gneiss::engine {
/** 逻辑像素描述；压缩容器的实际 GPU 格式仍由所选变体描述。 */
enum class texture_format : std::uint32_t { rgba8_unorm = 1 };
enum class texture_color_space : std::uint32_t { linear = 1, srgb = 2 };
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

/** 文件描述的运行时视图；尺寸和 mip 布局由负载容器负责，不重复存储。 */
struct texture_description final {
  std::string uri;
  texture_color_space color_space{};
};
} // namespace gneiss::engine
