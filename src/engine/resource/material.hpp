// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/asset.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/texture.hpp"
#include <array>

namespace gneiss::engine {
namespace detail {
struct material_resource_access;
}

struct material_content {
  material_parameters parameters;
  std::array<ref<const texture>, 5> textures;
};
/** 稳定运行身份，与 material_data 的 CPU 内容寿命分离。CPU 对象随最后强引用释放，只有后端 RID
 * 延迟回收。 */
class GNEISS_API material final : public asset {
public:
  ~material();
  /** 所属线程只读参数与依赖；更新通过 Loader/Render 受控入口执行。 */
  [[nodiscard]] const material_parameters& parameters() const noexcept {
    return content_ ? content_->parameters : empty_.parameters;
  }
  [[nodiscard]] const std::array<ref<const texture>, 5>& textures() const noexcept {
    return content_ ? content_->textures : empty_.textures;
  }
  [[nodiscard]] ref<const material_content> snapshot() const noexcept { return content_; }
  material(const material&) = delete;
  material& operator=(const material&) = delete;

private:
  friend struct detail::material_resource_access;
  material(rid value) noexcept;
  rid rid_;
  std::shared_ptr<void> retirement_token_;
  inline static const material_content empty_{};
  mutable ref<const material_content> content_;
};

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
[[nodiscard]] GNEISS_API rid resource_handle(const ref<const material>& value) noexcept;

} // namespace gneiss::engine
