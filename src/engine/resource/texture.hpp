// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/texture/texture_description.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gneiss::engine {
struct texture_data;
namespace detail {
struct texture_resource_access;
}

struct texture_metadata {
  std::uint32_t width{}, height{};
  std::optional<std::uint32_t> mip_levels;
  std::optional<texture_format> format;
  texture_color_space color_space{};
};

/** 稳定运行身份，与 texture_data 的 CPU 内容寿命分离。CPU 对象随最后强引用释放，只有后端 RID
 * 延迟回收。 */
class texture final : public asset {
public:
  GNEISS_API ~texture();
  /** 所属线程查询轻量信息；未加载时为空，内容保留不可变 CPU 快照。 */
  [[nodiscard]] const std::optional<texture_metadata>& metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] ref<const texture_data> snapshot() const { return content_; }
  texture(const texture&) = delete;
  texture& operator=(const texture&) = delete;

private:
  friend struct detail::texture_resource_access;
  GNEISS_API texture(rid value) noexcept;
  mutable ref<const texture_data> content_;
  rid rid_;
  std::shared_ptr<void> retirement_token_;
  mutable std::optional<texture_metadata> metadata_;
};

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
[[nodiscard]] GNEISS_API rid resource_handle(const ref<const texture>& value) noexcept;

} // namespace gneiss::engine
