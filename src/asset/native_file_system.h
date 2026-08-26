// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_NATIVE_FILE_SYSTEM_H_
#define GNEISS_ASSET_NATIVE_FILE_SYSTEM_H_

#include "asset/file_system.h"

#include <filesystem>

namespace gneiss::asset_internal {

class native_file_system final : public file_system {
public:
  [[nodiscard]] gneiss_result initialize(std::string_view root) noexcept;
  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override;
  [[nodiscard]] bool is_initialized() const noexcept { return !root_.empty(); }

private:
  std::filesystem::path root_;
};

} // namespace gneiss::asset_internal

#endif
