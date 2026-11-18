// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/resource/export.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/serialization/diagnostic.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace gneiss {
/** 逻辑像素描述；压缩容器的实际 GPU 格式仍由所选变体描述。 */
enum class texture_format : std::uint32_t { rgba8_unorm = 1 };
enum class texture_color_space : std::uint32_t { linear = 1, srgb = 2 };
enum class texture_filter : std::uint32_t { nearest, linear };
enum class texture_mip_filter : std::uint32_t { none, nearest, linear };
enum class texture_address : std::uint32_t { repeat, clamp, mirror };
struct texture_sampling {
  static constexpr std::string_view type_name = "texture_sampling";
  static constexpr guid type_guid = guid::literal("232103df-9e79-4d16-829e-3fcc84efebb8");
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
enum class texture_usage : std::uint32_t { default_usage, normal };
struct texture_description final {
  static constexpr std::string_view type_name = "texture_description";
  static constexpr guid type_guid = guid::literal("9405a373-53f4-43cc-94a5-64aed39e10cd");
  std::string uri;
  texture_color_space color_space{};
  texture_usage usage{};
};
/** 文件读写及原生采用前的领域校验；不修改输入。 */
[[nodiscard]] GNEISS_RESOURCE_API serialization::diagnostic
validate_texture_description(const texture_description&);
} // namespace gneiss
