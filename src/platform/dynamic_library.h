// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_PLATFORM_DYNAMIC_LIBRARY_H_
#define GNEISS_SRC_PLATFORM_DYNAMIC_LIBRARY_H_

#include <gneiss/core/result.hpp>

#include <filesystem>

namespace gneiss {

// 内部原生动态库句柄；调用方负责保证卸载前不再使用已查询的符号。
class dynamic_library final {
public:
  dynamic_library() noexcept = default;
  ~dynamic_library();

  dynamic_library(const dynamic_library&) = delete;
  dynamic_library& operator=(const dynamic_library&) = delete;

  [[nodiscard]] result open(const std::filesystem::path& path) noexcept;
  [[nodiscard]] result find_symbol(const char* name, void** out_symbol) const noexcept;
  void close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;

private:
  void* handle_{};
};

} // namespace gneiss

#endif
