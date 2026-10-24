// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gneiss::engine {

/** 同步文件来源，独立拥有打开对象；UTF-8 路径，允许并发区间读取。
 * 所有方法可能阻塞，只供明确允许阻塞的调用点使用；不创建 loop 或工作线程。
 * 不负责资产路径隔离、版本登记或内容解析。 */
class uv_file_reader final {
public:
  ~uv_file_reader();
  uv_file_reader(const uv_file_reader&) = delete;
  uv_file_reader& operator=(const uv_file_reader&) = delete;

  /** 失败清空输出；长度从已打开对象取得。仅接受普通文件。 */
  [[nodiscard]] static gneiss::engine::result
  open(std::string_view path, std::unique_ptr<uv_file_reader>& output) noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  /** 精确读取；越界为 invalid_argument，提前 EOF 为 io；失败后输出内容未定义。 */
  [[nodiscard]] gneiss::engine::result read_at(std::uint64_t offset,
                                               std::span<std::byte> output) const noexcept;
  /** 查询打开对象的当前长度，不按路径重新打开。 */
  [[nodiscard]] gneiss::engine::result current_size(std::uint64_t& output) const noexcept;

private:
  explicit uv_file_reader(int descriptor) noexcept;
  int descriptor_;
  std::uint64_t size_{};
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::engine
