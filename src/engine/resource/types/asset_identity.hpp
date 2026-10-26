// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/files/file_types.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace gneiss::engine {

/** 同来源、合法 URI 与类型标识同一逻辑资产；具体内容版本另行判断。
 * 持有来源标记避免地址复用导致身份误判，不延长文件后端或 GPU 资源寿命。
 */
struct asset_identity final {
  std::shared_ptr<const file_source_domain> source;
  std::string uri;
  std::uint32_t type{};
  bool operator==(const asset_identity&) const = default;
};

} // namespace gneiss::engine
