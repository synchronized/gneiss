// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_DIRECTORY_ASSET_PROVIDER_H_
#define GNEISS_ASSET_DIRECTORY_ASSET_PROVIDER_H_

#include <gneiss/core/result.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {

class directory_asset_provider final {
public:
  [[nodiscard]] gneiss_result mount(std::string_view root) noexcept;
  [[nodiscard]] gneiss_result read(std::string_view uri,
                                   std::vector<std::byte>& out_bytes) const noexcept;
  [[nodiscard]] bool is_mounted() const noexcept { return !root_.empty(); }

private:
  std::filesystem::path root_;
};

} // namespace gneiss::asset_internal

#endif
