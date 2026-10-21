// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

/** 一次挂载的运行期来源身份；不包含后端，不作为持久 AssetId 或内容版本。 */
struct asset_source_domain final {};

/** 同来源、合法 URI 与类型标识同一逻辑资产；具体内容版本另行判断。
 * 持有来源标记避免地址复用导致身份误判，不延长文件后端或 GPU 资源寿命。
 */
struct asset_identity final {
  std::shared_ptr<const asset_source_domain> source;
  std::string uri;
  std::uint32_t type{};
  bool operator==(const asset_identity&) const = default;
};

/** 运行期产物版本；version 编码内容、依赖与准备配置，不是持久资产 ID。
 * inputs 按编码顺序保留直接和传递输入的来源身份，防止来源标记地址复用。
 * 相同摘要但输入来源不同仍是不同版本。 */
struct asset_version_key final {
  asset_identity identity;
  std::string version;
  std::vector<asset_identity> inputs{};
  bool operator==(const asset_version_key&) const = default;
};

} // namespace gneiss::asset_internal
