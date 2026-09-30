// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset/read_source.h"
#include "asset/texture_binary.h"

#include <memory>

namespace gneiss::asset_internal {

/** 封装读取器只驻留 Manifest，拥有读取来源；不提供内容快照或设备变体选择。
 * 打开后可并发只读；open 不得与其他调用并发。Manifest 语义由渲染资产适配层检查。 */
class texture_container final {
public:
  /** 无论成功失败均接管 source；失败清空旧状态。limit 约束 Manifest 分配，不含固定头和填充。 */
  [[nodiscard]] gneiss_result open(std::unique_ptr<read_source> source, std::size_t manifest_limit,
                                   std::string& diagnostic) noexcept;
  /** 返回借用视图，在容器析构或下次 open 时失效。 */
  [[nodiscard]] std::span<const std::byte> manifest() const noexcept { return manifest_; }
  [[nodiscard]] std::uint64_t payload_size() const noexcept { return layout_.payload_size; }
  /** offset 相对于 Payload 起点；只读取调用者提供的缓冲范围，不分配负载。 */
  [[nodiscard]] gneiss_result read_payload(std::uint64_t offset,
                                           std::span<std::byte> output) const noexcept;

private:
  std::unique_ptr<read_source> source_;
  texture_binary_layout layout_{};
  std::vector<std::byte> manifest_;
};

} // namespace gneiss::asset_internal
