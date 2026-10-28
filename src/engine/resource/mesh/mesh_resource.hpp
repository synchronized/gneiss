// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/resource_cache.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gneiss::engine {
class render_asset_loader;
class render_service;
class resource_bindings;
class resource_retirement_queue;

struct mesh_metadata {
  std::size_t vertices{};
  std::size_t indices{};
  bool has_normals{};
  bool has_uv1{};
};

/** 稳定运行身份，与 mesh_data 的 CPU 内容寿命分离。由 Render 创建并延迟析构。 */
class GNEISS_API mesh_resource final {
public:
  [[nodiscard]] const asset_identity& source() const noexcept { return identity_; }
  ~mesh_resource();
  /** 所属线程查询轻量信息；未加载时为空，不持有 CPU 数组。 */
  [[nodiscard]] const std::optional<mesh_metadata>& metadata() const noexcept { return metadata_; }
  mesh_resource(const mesh_resource&) = delete;
  mesh_resource& operator=(const mesh_resource&) = delete;

private:
  friend class resource_retirement_queue;
  mesh_resource(rid value) noexcept;
  friend rid resource_handle(const asset_ref<const mesh_resource>&) noexcept;
  friend class render_asset_loader;
  friend class resource_bindings;
  friend rid asset_rid(const asset_ref<const resource_cache::entry>&) noexcept;
  rid rid_;
  mutable asset_identity identity_;
  mutable std::uint64_t update_sequence_{};
  mutable std::optional<mesh_metadata> metadata_;
};

using mesh = asset_ref<const mesh_resource>;

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
inline rid resource_handle(const mesh& value) noexcept { return value ? value->rid_ : null_rid; }

/** 所属线程取得 RID 的唯一资源引用；无效 RID 返回空，分配失败抛出 bad_alloc。 */
[[nodiscard]] GNEISS_API mesh reference_mesh(render_service& render, rid handle);

} // namespace gneiss::engine
