// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_VIRTUAL_FILE_SYSTEM_HPP_
#define GNEISS_ASSET_VIRTUAL_FILE_SYSTEM_HPP_

#include "engine/resource/asset_identity.hpp"
#include "engine/resource/file_system.hpp"

#include <memory>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

class virtual_file_system final {
public:
  /** 仅解析挂载身份，不读取文件或验证存在性；失败清空 output。
   * VFS 副本共享已有挂载身份，新挂载使用新身份；挂载后的后端不得重新指向其他来源。
   * 无并发挂载时允许并发查询。类型零无效，URI 保留大小写。
   * 透明后端可转交原始来源身份；其查询失败不会回退到包装挂载身份。
   */
  [[nodiscard]] gneiss_result identify(std::string_view uri, std::uint32_t type,
                                       asset_identity& output) const noexcept;
  [[nodiscard]] gneiss_result open_read(std::string_view uri,
                                        std::unique_ptr<read_source>& output) const noexcept;
  [[nodiscard]] gneiss_result
  open_read_for_validation(std::string_view uri,
                           std::unique_ptr<read_source>& output) const noexcept;
  [[nodiscard]] gneiss_result
  begin_open_read_for_validation(std::string_view uri,
                                 std::unique_ptr<open_read_operation>& output) const noexcept;
  [[nodiscard]] gneiss_result mount(std::string_view mount_point,
                                    std::shared_ptr<file_system> backend) noexcept;
  [[nodiscard]] gneiss_result read(std::string_view uri,
                                   std::vector<std::byte>& out_bytes) const noexcept;
  [[nodiscard]] gneiss_result read_bounded(std::string_view uri, std::size_t limit,
                                           std::vector<std::byte>& out_bytes) const noexcept;
  [[nodiscard]] std::size_t mount_count() const noexcept { return mounts_.size(); }

private:
  gneiss_result open_impl(std::string_view uri, std::unique_ptr<read_source>& output,
                          bool deferred_validation) const noexcept;
  struct mount_entry final {
    std::string point;
    std::shared_ptr<file_system> backend;
    std::shared_ptr<const asset_source_domain> source;
  };

  std::vector<mount_entry> mounts_;
};

} // namespace gneiss::asset_internal

#endif
