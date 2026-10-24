// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_FILE_SYSTEM_HPP_
#define GNEISS_ASSET_FILE_SYSTEM_HPP_

#include "engine/core/result.hpp"

#include "engine/resource/types/asset_identity.hpp"
#include "engine/resource/vfs/read_source.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace gneiss::engine::resource {

/** VFS 后端的最小只读接口。路径相对于挂载点，且不包含 scheme。 */
class file_system {
public:
  virtual ~file_system() = default;

  /** 透明读取包装可转交原始来源身份；不得读取文件或以身份代替来源复验。
   * 默认 UNSUPPORTED 由 VFS 使用当前挂载身份；其他错误必须传递，不能回退为新身份。
   */
  [[nodiscard]] virtual gneiss::engine::core::result
  identify_source(std::string_view, std::uint32_t, asset_identity& output) const noexcept {
    output = {};
    return gneiss::engine::core::result::unsupported;
  }

  /** 打开区间读取来源；失败清空输出。默认不支持，禁止整文件模拟按需读取。 */
  [[nodiscard]] virtual gneiss::engine::core::result
  open_read(std::string_view, std::unique_ptr<read_source>& output) const noexcept {
    output.reset();
    return gneiss::engine::core::result::unsupported;
  }
  /** 供分步准备使用；版本包装延迟全文件扫描，调用者必须完成来源的验证回执后再发布。 */
  [[nodiscard]] virtual gneiss::engine::core::result
  open_read_for_validation(std::string_view path,
                           std::unique_ptr<read_source>& output) const noexcept {
    return open_read(path, output);
  }

  /** 可选异步打开；不支持时由调用方显式选择同步回退，禁止在本入口伪装异步。 */
  [[nodiscard]] virtual gneiss::engine::core::result
  begin_open_read_for_validation(std::string_view,
                                 std::unique_ptr<open_read_operation>& output) const noexcept {
    output.reset();
    return gneiss::engine::core::result::unsupported;
  }

  file_system(const file_system&) = delete;
  file_system& operator=(const file_system&) = delete;
  file_system(file_system&&) = delete;
  file_system& operator=(file_system&&) = delete;

  [[nodiscard]] virtual gneiss::engine::core::result
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept = 0;

  /** 有界读取必须在分配前检查；未实现此能力的后端不可用于异步准备。 */
  [[nodiscard]] virtual gneiss::engine::core::result
  read_bounded(std::string_view, std::size_t, std::vector<std::byte>&) const noexcept {
    return gneiss::engine::core::result::unsupported;
  }

protected:
  file_system() = default;
};

} // namespace gneiss::engine::resource

#endif
