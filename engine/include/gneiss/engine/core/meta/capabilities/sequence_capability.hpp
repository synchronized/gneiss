// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/capabilities/container_capability.hpp>

namespace gneiss::meta {
/** 不依赖容器实例的类型能力；保活类型上下文，不复制能力数据。 */
class sequence_capability : public container_capability<type_kind::sequence, basic_sequence_view> {
public:
  using container_capability::container_capability;
  /** 下列查询要求有效能力视图。 */
  [[nodiscard]] GNEISS_API bool is_fixed_size() const noexcept;
  [[nodiscard]] GNEISS_API bool has_identity() const noexcept;
  [[nodiscard]] GNEISS_API meta::type element_type() const;
};
}
