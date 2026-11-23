// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/resource/export.hpp>

#include <gneiss/engine/export.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/resource/asset.hpp>
#include <gneiss/engine/resource/asset_ref.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gneiss {
struct mesh_data;
namespace detail {
struct mesh_resource_access;
}

struct mesh_metadata {
  std::size_t vertices{};
  std::size_t indices{};
  bool has_normals{};
  bool has_uv1{};
};

/** 稳定运行身份，与 mesh_data 的 CPU 内容寿命分离。CPU 对象随最后强引用释放，只有后端 RID
 * 延迟回收。 */
class GNEISS_RESOURCE_TYPE_API mesh final : public asset {
public:
  GNEISS_RESOURCE_API ~mesh();
  /** 所属线程查询轻量信息；未加载时为空，内容保留不可变 CPU 快照。 */
  [[nodiscard]] const std::optional<mesh_metadata>& metadata() const noexcept { return metadata_; }
  [[nodiscard]] ref<const mesh_data> snapshot() const { return content_; }
  mesh(const mesh&) = delete;
  mesh& operator=(const mesh&) = delete;

private:
  friend struct detail::mesh_resource_access;
  GNEISS_RESOURCE_API mesh(rid value) noexcept;
  mutable ref<const mesh_data> content_;
  rid rid_;
  std::shared_ptr<void> retirement_token_;
  mutable std::optional<mesh_metadata> metadata_;
};

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
[[nodiscard]] GNEISS_RESOURCE_API rid resource_handle(const ref<const mesh>& value) noexcept;

} // namespace gneiss
