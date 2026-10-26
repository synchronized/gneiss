// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include "legacy_file_backend.hpp"
#include <limits>
namespace gneiss::test_support {
// 仅用于未迁移测试夹具，生产 Manager 不再接受旧 VFS。
inline std::shared_ptr<engine::file_service> test_files(const engine::virtual_file_system& files) {
  return engine::make_file_service(
      files, {.max_requests = 64, .max_file_bytes = std::numeric_limits<std::size_t>::max()});
}
inline std::shared_ptr<engine::file_service>
test_files(std::shared_ptr<engine::file_service> files) {
  return files;
}
}
