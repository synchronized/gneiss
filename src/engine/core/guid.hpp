// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_GUID_HPP_
#define GNEISS_ENGINE_GUID_HPP_

#include <gneiss/engine/core/export.h>

#include <array>
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

  [[nodiscard]] static constexpr guid nil() noexcept { return guid{}; }
  [[nodiscard]] static constexpr guid from_bytes(byte_array bytes) noexcept { return guid{bytes}; }
  [[nodiscard]] static std::optional<guid> parse(std::string_view text);

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

private:
  byte_array bytes_{};
};

/** @brief `guid` 的哈希函数，供 unordered 容器使用。 */
struct GNEISS_API guid_hash {
  [[nodiscard]] std::size_t operator()(const guid& value) const noexcept;
};

} // namespace gneiss::engine

#endif
