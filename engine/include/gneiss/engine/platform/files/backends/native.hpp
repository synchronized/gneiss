// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/platform/files/backend.hpp>
namespace gneiss {
namespace io {
class executor;
}
}
namespace gneiss::files {
struct native_config {
  io::executor& executor;
  std::string root;
};
/** 借用执行器至请求收尾；根目录须存在，写入不创建父目录。 */
[[nodiscard]] GNEISS_API std::shared_ptr<backend> create_backend_native(native_config config);
} // namespace gneiss::files
