// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/files/backend.hpp>
namespace gneiss::files {
error open_fetch_stream(std::string url, files::callback<std::shared_ptr<backend_stream>>);
}
