// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/texture/texture_resource.hpp"
#include <array>

namespace gneiss::engine {
namespace detail {
struct material_resource_access;
}

/** 稳定运行身份，与 material_data 的 CPU 内容寿命分离。由资源工厂创建并延迟析构。 */
class GNEISS_API material_resource final {
public:
  [[nodiscard]] const asset_identity& source() const noexcept { return identity_; }
  ~material_resource();
  /** 所属线程只读参数与依赖；更新通过 Loader/Render 受控入口执行。 */
  [[nodiscard]] const material_parameters& parameters() const noexcept { return parameters_; }
  [[nodiscard]] const std::array<texture, 5>& textures() const noexcept { return textures_; }
  material_resource(const material_resource&) = delete;
  material_resource& operator=(const material_resource&) = delete;

private:
  friend struct detail::material_resource_access;
  material_resource(rid value, std::array<texture, 5> dependencies = {}) noexcept;
  rid rid_;
  mutable asset_identity identity_;
  mutable std::uint64_t update_sequence_{};
  mutable material_parameters parameters_;
  mutable std::array<texture, 5> textures_;
};

using material = asset_ref<const material_resource>;

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
[[nodiscard]] GNEISS_API rid resource_handle(const material& value) noexcept;

} // namespace gneiss::engine
