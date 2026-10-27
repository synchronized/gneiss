// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
namespace gneiss::engine::files {
result open_fetch_stream(std::string url, files::callback<std::shared_ptr<backend_stream>>);
}
