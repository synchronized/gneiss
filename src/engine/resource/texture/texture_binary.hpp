// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_TEXTURE_BINARY_HPP_
#define GNEISS_ASSET_TEXTURE_BINARY_HPP_

#include "engine/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::engine {

struct texture_binary_view final {
  std::span<const std::byte> manifest;
  std::span<const std::byte> payload;
};

/** 已校验的封装布局；偏移均相对于文件起点，不包含 Manifest 内部语义。 */
struct texture_binary_layout final {
  std::uint64_t manifest_offset{};
  std::uint64_t manifest_size{};
  std::uint64_t payload_offset{};
  std::uint64_t payload_size{};
};

enum class texture_binary_error : std::uint8_t {
  invalid_argument,
  invalid_container,
  unsupported_version,
  out_of_memory,
};

/** 编解码失败原因；message 拥有文本，分配失败时可为空。 */
struct texture_binary_diagnostic final {
  texture_binary_error error{texture_binary_error::invalid_argument};
  std::string message;
};

[[nodiscard]] bool is_texture_binary(std::span<const std::byte> bytes) noexcept;
/** 仅解析 64 字节头；source_size 为完整来源长度，不读取负载或校验填充。 */
[[nodiscard]] result<texture_binary_layout, texture_binary_diagnostic>
decode_texture_binary_header(std::span<const std::byte> bytes, std::uint64_t source_size) noexcept;
/** 成功结果拥有完整封装字节；失败不返回部分数据。 */
[[nodiscard]] result<std::vector<std::byte>, texture_binary_diagnostic>
encode_texture_binary(std::span<const std::byte> manifest,
                      std::span<const std::byte> payload) noexcept;
/** 成功视图借用输入字节，其有效期不得超过输入存储。 */
[[nodiscard]] result<texture_binary_view, texture_binary_diagnostic>
decode_texture_binary(std::span<const std::byte> bytes) noexcept;

} // namespace gneiss::engine
#endif
