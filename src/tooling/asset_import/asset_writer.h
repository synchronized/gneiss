// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "tooling/asset_import/import_ir.h"

#include <filesystem>
#include <string>

namespace gneiss::tooling::asset_import {

struct write_report {
  bool success{};
  std::string diagnostic;
};

[[nodiscard]] write_report write_assets(const import_ir& data,
                                        const std::filesystem::path& output_directory);

} // namespace gneiss::tooling::asset_import
