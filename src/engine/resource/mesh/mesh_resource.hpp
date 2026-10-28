// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_ref.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gneiss::engine {
namespace detail {
struct mesh_resource_access;
}

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
  friend struct detail::mesh_resource_access;
  mesh_resource(rid value) noexcept;
  rid rid_;
  mutable asset_identity identity_;
  mutable std::uint64_t update_sequence_{};
  mutable std::optional<mesh_metadata> metadata_;
};

using mesh = asset_ref<const mesh_resource>;

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
[[nodiscard]] GNEISS_API rid resource_handle(const mesh& value) noexcept;

} // namespace gneiss::engine
