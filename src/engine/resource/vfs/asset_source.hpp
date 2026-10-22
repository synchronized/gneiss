// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "virtual_file_system.hpp"
#include <gneiss/engine/core/result.hpp>

namespace gneiss::engine::resource {
/** 示例 Source 的 VFS 接入；一次操作串行推进，每步最多读取 64 KiB。
 * 优先异步打开/区间读取；无此能力时在执行器中有界同步回退，不承诺单步耗时。
 * 销毁取消接收，后端保活并回收实际在途操作。 */
class asset_source final {
public:
  asset_source(virtual_file_system files, std::string uri, std::size_t limit);
  [[nodiscard]] result advance();
  [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

private:
  [[nodiscard]] result read_contents();
  virtual_file_system files_;
  std::string uri_;
  std::size_t limit_;
  bool started_{}, closing_{};
  std::unique_ptr<open_read_operation> opening_;
  std::unique_ptr<read_source> source_;
  std::unique_ptr<read_operation> reading_;
  std::vector<std::byte> bytes_;
};
}
