// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/texture/texture_description.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gneiss::engine {
namespace detail {
struct texture_resource_access;
}

struct texture_metadata {
  std::uint32_t width{}, height{};
  std::optional<std::uint32_t> mip_levels;
  std::optional<texture_format> format;
  texture_color_space color_space{};
};

/** 稳定运行身份，与 texture_data 的 CPU 内容寿命分离。由 Render 创建并延迟析构。 */
class GNEISS_API texture_resource final {
public:
  [[nodiscard]] const asset_identity& source() const noexcept { return identity_; }
  ~texture_resource();
  /** 所属线程查询轻量信息；未加载时为空，不持有 CPU 数组。 */
  [[nodiscard]] const std::optional<texture_metadata>& metadata() const noexcept {
    return metadata_;
  }
  texture_resource(const texture_resource&) = delete;
  texture_resource& operator=(const texture_resource&) = delete;

private:
  friend struct detail::texture_resource_access;
  texture_resource(rid value) noexcept;
  rid rid_;
  mutable asset_identity identity_;
  mutable std::uint64_t update_sequence_{};
  mutable std::optional<texture_metadata> metadata_;
};

using texture = asset_ref<const texture_resource>;

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
[[nodiscard]] GNEISS_API rid resource_handle(const texture& value) noexcept;

} // namespace gneiss::engine
