// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "dynamic_library.hpp"

int main(int argc, char** argv) try {
  if (argc != 2) {
    return 1;
  }
  gneiss::dynamic_library library;
  void* symbol = nullptr;
  if (library.is_open() ||
      library.find_symbol("gneiss_test_library_value", &symbol) !=
          gneiss::result::invalid_argument ||
      library.open({}) != gneiss::result::invalid_state ||
      library.open(std::filesystem::path{argv[1]} / "missing") != gneiss::result::not_found) {
    return 2;
  }
  if (library.open(argv[1]).failed() || !library.is_open() ||
      library.open(argv[1]) != gneiss::result::invalid_state ||
      library.find_symbol("", &symbol) != gneiss::result::invalid_argument ||
      library.find_symbol("gneiss_test_library_value", nullptr) !=
          gneiss::result::invalid_argument ||
      library.find_symbol("gneiss_test_library_missing", &symbol) != gneiss::result::not_found ||
      symbol != nullptr || library.find_symbol("gneiss_test_library_value", &symbol).failed() ||
      symbol == nullptr) {
    return 3;
  }
  const auto function = reinterpret_cast<int (*)()>(symbol);
  if (function() != 42) {
    return 4;
  }
  // 关闭后不调用旧符号；重复关闭与重新加载必须有效。
  library.close();
  library.close();
  if (library.is_open() ||
      library.find_symbol("gneiss_test_library_value", &symbol) !=
          gneiss::result::invalid_argument ||
      library.open(argv[1]).failed()) {
    return 5;
  }
  return 0;
} catch (...) {
  // 路径转换等标准库操作失败时返回测试失败，避免弹出未处理异常对话框。
  return 6;
}
