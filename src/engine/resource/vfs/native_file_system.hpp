// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_NATIVE_FILE_SYSTEM_HPP_
#define GNEISS_ASSET_NATIVE_FILE_SYSTEM_HPP_

#include "engine/resource/vfs/file_system.hpp"

#include <filesystem>

namespace gneiss::engine::platform::io {
class uv_file_io;
class uv_path_resolver;
}
namespace gneiss::engine::resource {

class native_file_system final : public file_system {
public:
#ifdef GNEISS_ASSET_LIBUV
  /** 宿主在线程模式显式注入；挂载开始服务后不再改变。 */
  void set_async_io(std::shared_ptr<engine::platform::io::uv_file_io> files,
                    std::shared_ptr<engine::platform::io::uv_path_resolver> paths) noexcept;
  [[nodiscard]] gneiss_result begin_open_read_for_validation(
      std::string_view path, std::unique_ptr<open_read_operation>& output) const noexcept override;
#endif
  [[nodiscard]] gneiss_result initialize(std::string_view root) noexcept;
  [[nodiscard]] gneiss_result
  open_read(std::string_view path, std::unique_ptr<read_source>& output) const noexcept override;
  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override;
  [[nodiscard]] gneiss_result
  read_bounded(std::string_view path, std::size_t limit,
               std::vector<std::byte>& out_bytes) const noexcept override;
  [[nodiscard]] bool is_initialized() const noexcept { return !root_.empty(); }

private:
  std::filesystem::path root_;
#ifdef GNEISS_ASSET_LIBUV
  std::shared_ptr<engine::platform::io::uv_file_io> async_files_;
  std::shared_ptr<engine::platform::io::uv_path_resolver> async_paths_;
#endif
};

} // namespace gneiss::engine::resource

#endif
