// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/logging/sink.hpp>
#include <filesystem>
#include <memory>
namespace gneiss::host {
std::unique_ptr<logging::sink> create_runtime_protocol_sink();
std::filesystem::path default_runtime_log_path();
}
