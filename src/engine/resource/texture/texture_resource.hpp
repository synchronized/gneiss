// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/resource_cache.hpp"
#include "engine/resource/texture/texture_description.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gneiss::engine {
class render_asset_loader;
class render_service;
class resource_bindings;
class resource_retirement_queue;

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
  friend class resource_retirement_queue;
  texture_resource(rid value) noexcept;
  friend rid resource_handle(const asset_ref<const texture_resource>&) noexcept;
  friend class render_asset_loader;
  friend class resource_bindings;
  friend rid asset_rid(const asset_ref<const resource_cache::entry>&) noexcept;
  rid rid_;
  mutable asset_identity identity_;
  mutable std::uint64_t update_sequence_{};
  mutable std::optional<texture_metadata> metadata_;
};

using texture = asset_ref<const texture_resource>;

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
inline rid resource_handle(const texture& value) noexcept { return value ? value->rid_ : null_rid; }

/** 所属线程取得 RID 的唯一资源引用；无效 RID 返回空，分配失败抛出 bad_alloc。 */
[[nodiscard]] GNEISS_API texture reference_texture(render_service& render, rid handle);
/** 程序化纹理入口；复制像素，成功后由强引用负责销毁。 */
[[nodiscard]] GNEISS_API result create_texture(render_service& render, const texture_view& desc,
                                               texture& output) noexcept;

} // namespace gneiss::engine
