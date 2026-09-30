// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/core/result.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace gneiss::asset_internal {

/** 独立拥有底层读取对象，可在创建它的 VFS 销毁后使用。
 * 同一来源允许并发读取；不保证外部文件内容不可变，版本校验由资产层负责。 */
class read_source {
public:
  virtual ~read_source() = default;
  read_source(const read_source&) = delete;
  read_source& operator=(const read_source&) = delete;

  /** 打开时的长度；不随外部文件增长而扩大可读范围。 */
  [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
  /** 精确读取指定范围，不分配负载缓冲。空范围允许位于 EOF。
   * 越界返回 INVALID_ARGUMENT；短读返回 IO。失败后目标缓冲内容未定义。 */
  [[nodiscard]] virtual gneiss_result read_at(std::uint64_t offset,
                                              std::span<std::byte> output) const noexcept = 0;

protected:
  read_source() = default;
};

} // namespace gneiss::asset_internal
