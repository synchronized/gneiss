// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include "engine/core/runtime_type_id.hpp"
#include <memory>
#include <vector>

namespace gneiss::engine {
/** 从 Granit 示例移植的 CPU Loader 契约；只解析字节，不创建 GPU 或场景对象。
 * 同一 Loader 可并发解码；输出失败时不发布。类型无需继承资源基类或注册字段反射。 */
class asset_loader {
public:
  virtual ~asset_loader() = default;
  [[nodiscard]] virtual gneiss::engine::runtime_type_id target_type() const noexcept = 0;
  [[nodiscard]] virtual std::size_t maximum_bytes() const noexcept = 0;
  [[nodiscard]] virtual gneiss::engine::result
  decode(const std::vector<std::byte>& bytes, std::shared_ptr<const void>& output) const = 0;
};
}
