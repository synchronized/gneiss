// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "tooling/asset_import/import_ir.h"

#include <optional>
#include <string>

namespace gneiss::tooling::asset_import {

/** 按面角生成切线，必要时拆分镜像 UV 接缝；失败不修改 Primitive。 */
[[nodiscard]] std::optional<std::string> generate_tangents(import_ir_primitive& primitive);

} // namespace gneiss::tooling::asset_import
