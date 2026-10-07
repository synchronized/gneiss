// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/sha256.hpp"
#include <gneiss/engine/core/result.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gneiss::asset_internal {

/** 独立拥有一次区间读取和缓冲；销毁时取消并回收未完成操作。
 * poll 不等待 I/O，NOT_READY 表示尚未完成。成功视图有效至请求销毁。 */
class read_operation {
public:
  virtual ~read_operation() = default;
  read_operation(const read_operation&) = delete;
  read_operation& operator=(const read_operation&) = delete;
  [[nodiscard]] virtual gneiss_result poll(std::span<const std::byte>& output) noexcept = 0;
  /** 仅允许可阻塞的工作线程请求有限等待；零预算保持轮询。
   * 默认后端不等待，返回 poll 结果；成功视图与 poll 相同，超时返回 NOT_READY。 */
  [[nodiscard]] virtual gneiss_result wait_for(std::chrono::milliseconds timeout,
                                               std::span<const std::byte>& output) noexcept {
    if (timeout < std::chrono::milliseconds::zero()) {
      output = {};
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    return poll(output);
  }

protected:
  read_operation() = default;
};

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
  /** 可选的可轮询区间读取；不支持时返回 UNSUPPORTED，调用方决定同步回退。
   * 成功请求独立持有底层对象，不借用调用方输出缓冲。 */
  [[nodiscard]] virtual gneiss_result
  begin_read(std::uint64_t /*offset*/, std::size_t /*size*/,
             std::unique_ptr<read_operation>& output) const noexcept {
    output.reset();
    return GNEISS_ERROR_UNSUPPORTED;
  }
  /** 分步读取入口的验证回执；调用方须已完整扫描本来源并计算摘要。
   * 版本包装登记内容身份，普通文件默认无需登记；不提供外部文件不可变保证。 */
  [[nodiscard]] virtual gneiss_result
  complete_validation(const core::sha256_digest& /*digest*/) const noexcept {
    return GNEISS_SUCCESS;
  }

protected:
  read_source() = default;
};

} // namespace gneiss::asset_internal
