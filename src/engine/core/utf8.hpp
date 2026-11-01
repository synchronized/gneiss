// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gneiss::engine::utf8 {
/** 校验完整 UTF-8 字节序列，不分配、不转换或规范化文本。
 * 空串及 NUL 是合法编码；名称、路径等业务限制由调用者检查。
 * 拒绝截断、孤立续字节、过长编码、代理码点和超出 Unicode 范围的码点。 */
[[nodiscard]] inline bool is_valid(std::string_view text) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first < 0x80U) {
      ++index;
      continue;
    }
    std::size_t count = 0;
    std::uint32_t value = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
      count = 1;
      value = first & 0x1FU;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      count = 2;
      value = first & 0x0FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      count = 3;
      value = first & 0x07U;
    } else {
      return false;
    }
    if (count > text.size() - index - 1) {
      return false;
    }
    for (std::size_t offset = 1; offset <= count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      value = (value << 6U) | (next & 0x3FU);
    }
    if ((count == 2 && value < 0x800U) || (count == 3 && value < 0x10000U) || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
      return false;
    }
    index += count + 1;
  }
  return true;
}

} // namespace gneiss::engine::utf8
