// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_VIRTUAL_FILE_SYSTEM_H_
#define GNEISS_ASSET_VIRTUAL_FILE_SYSTEM_H_

#include "asset/file_system.h"

#include <memory>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

class virtual_file_system final {
public:
  [[nodiscard]] gneiss_result mount(std::string_view mount_point,
                                    std::shared_ptr<file_system> backend) noexcept;
  [[nodiscard]] gneiss_result read(std::string_view uri,
                                   std::vector<std::byte>& out_bytes) const noexcept;
  [[nodiscard]] std::size_t mount_count() const noexcept { return mounts_.size(); }

private:
  struct mount_entry final {
    std::string point;
    std::shared_ptr<file_system> backend;
  };

  std::vector<mount_entry> mounts_;
};

} // namespace gneiss::asset_internal

#endif
