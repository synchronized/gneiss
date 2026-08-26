// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_FILE_SYSTEM_H_
#define GNEISS_ASSET_FILE_SYSTEM_H_

#include <gneiss/core/result.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {

/** VFS 后端的最小只读接口。路径相对于挂载点，且不包含 scheme。 */
class file_system {
public:
  virtual ~file_system() = default;

  file_system(const file_system&) = delete;
  file_system& operator=(const file_system&) = delete;
  file_system(file_system&&) = delete;
  file_system& operator=(file_system&&) = delete;

  [[nodiscard]] virtual gneiss_result read(std::string_view path,
                                           std::vector<std::byte>& out_bytes) const noexcept = 0;

protected:
  file_system() = default;
};

} // namespace gneiss::asset_internal

#endif
