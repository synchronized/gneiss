// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_GUID_HPP_
#define GNEISS_ENGINE_GUID_HPP_

#include "engine/core/export.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gneiss::engine {

/**
 * @brief 128-bit 全局唯一标识值类型。
 *
 * `guid` 保存标准 UUID/GUID 的 16 字节值，可用于类型、资源、资产和序列化对象等需要稳定
 * 标识的场景。默认构造得到 nil GUID。
 */
class GNEISS_API guid {
public:
  using byte_array = std::array<std::uint8_t, 16>;

  constexpr guid() noexcept = default;
  explicit constexpr guid(byte_array bytes) noexcept : bytes_(bytes) {}

  /** 创建随机 UUID v4；随机源失败时抛出，由调用边界转换为 error。 */
  [[nodiscard]] static guid generate();
  [[nodiscard]] static constexpr guid nil() noexcept { return guid{}; }
  [[nodiscard]] static constexpr guid from_bytes(byte_array bytes) noexcept { return guid{bytes}; }
  /** 标准 UUID 文本解析；接受大小写十六进制，格式错误返回空值。 */
  [[nodiscard]] static constexpr std::optional<guid> parse(std::string_view text) noexcept;
  /** 编译期固定标识；格式错误直接导致编译失败，不生成运行期解析。 */
  [[nodiscard]] static consteval guid literal(std::string_view text);

  [[nodiscard]] constexpr const byte_array& bytes() const noexcept { return bytes_; }
  [[nodiscard]] bool is_nil() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return !is_nil(); }
  [[nodiscard]] std::string string() const;

  friend constexpr bool operator==(const guid& lhs, const guid& rhs) noexcept {
    return lhs.bytes_ == rhs.bytes_;
  }

  friend constexpr bool operator!=(const guid& lhs, const guid& rhs) noexcept {
    return !(lhs == rhs);
  }

  /** 按标准 UUID 字节顺序比较，供确定性排序使用。 */
  friend constexpr auto operator<=>(const guid&, const guid&) noexcept = default;

private:
  byte_array bytes_{};
};

constexpr std::optional<guid> guid::parse(std::string_view text) noexcept {
  if (text.size() != 36)
    return std::nullopt;
  const auto hex = [](char c) constexpr {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  byte_array bytes{};
  std::size_t cursor = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      if (text[cursor++] != '-')
        return std::nullopt;
    }
    const auto high = hex(text[cursor++]);
    const auto low = hex(text[cursor++]);
    if (high < 0 || low < 0)
      return std::nullopt;
    bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return guid{bytes};
}

consteval guid guid::literal(std::string_view text) {
  const auto parsed = parse(text);
  if (!parsed)
    throw "guid::literal requires a UUID in xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx form";
  return *parsed;
}

/** @brief `guid` 的哈希函数，供 unordered 容器使用。 */
struct GNEISS_API guid_hash {
  [[nodiscard]] std::size_t operator()(const guid& value) const noexcept;
};

} // namespace gneiss::engine

#endif
