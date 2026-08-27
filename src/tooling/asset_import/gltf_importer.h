// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "tooling/asset_import/import_ir.h"

#include <filesystem>
#include <string>

namespace gneiss::tooling::asset_import {

enum class inspect_result {
  success,
  invalid_argument,
  source_unavailable,
  invalid_source,
  unsupported_feature,
};

struct inspect_report {
  inspect_result result{inspect_result::invalid_argument};
  import_ir_summary summary{};
  import_ir data;
  std::string diagnostic;
};

[[nodiscard]] inspect_report inspect_gltf(const std::filesystem::path& source_path);

} // namespace gneiss::tooling::asset_import
