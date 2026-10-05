// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>

namespace gneiss::editor {

/** Runtime 对象身份；generation 防止重用对象编号误匹配。 */
struct runtime_object_id final {
  std::uint64_t value = 0U;
  std::uint32_t generation = 0U;
  [[nodiscard]] bool is_valid() const noexcept { return value != 0U && generation != 0U; }
  [[nodiscard]] bool operator==(const runtime_object_id&) const noexcept = default;
};

} // namespace gneiss::editor
