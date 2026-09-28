// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/source_revision_file_system.h"

#include <cstdio>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss::asset_internal;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("源版本契约失败，行=" + std::to_string(where.line()));
  }
}
struct memory_files final : file_system {
  std::string text{"abc"};
  bool missing{};
  gneiss_result read(std::string_view path, std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, 1024U, bytes);
  }
  gneiss_result read_bounded(std::string_view, std::size_t limit,
                             std::vector<std::byte>& bytes) const noexcept override try {
    bytes.clear();
    if (missing) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (text.size() > limit) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto view = std::as_bytes(std::span{text});
    bytes.assign(view.begin(), view.end());
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
};
void run() {
  auto memory = std::make_shared<memory_files>();
  virtual_file_system files;
  check(files.mount("asset://", memory) == GNEISS_SUCCESS);
  source_revision_file_system tracked(files);
  std::vector<std::byte> bytes;
  check(tracked.read_bounded("scene.json", 3U, bytes) == GNEISS_SUCCESS);
  check(tracked.read("scene.json", bytes) == GNEISS_SUCCESS);
  check(tracked.source_count() == 1U);
  check(tracked.verify({}) == GNEISS_SUCCESS);
  check(tracked.verify([] { return true; }) == GNEISS_ERROR_INVALID_STATE);
  memory->text = "abd";
  check(tracked.read("scene.json", bytes) == GNEISS_ERROR_INVALID_STATE && bytes.empty());
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  // 后续批次读新 URI 不得覆盖先前 URI 的版本身份。
  check(tracked.read("texture.png", bytes) == GNEISS_SUCCESS);
  check(tracked.source_count() == 2U);
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  memory->text = "abcd";
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  memory->text = "ab";
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  memory->missing = true;
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  memory->missing = false;
  memory->text = "abc";
  // 新会话独立建账；原始 VFS 保持可更新，不受旧会话约束。
  source_revision_file_system retry(files);
  check(retry.read("scene.json", bytes) == GNEISS_SUCCESS);
  check(retry.verify({}) == GNEISS_SUCCESS);
  memory->text = "new";
  check(files.read("asset://scene.json", bytes) == GNEISS_SUCCESS);
  check(retry.read_bounded("other", 2U, bytes) == GNEISS_ERROR_INVALID_ARGUMENT);
  check(retry.source_count() == 1U);
}
} // namespace
int main() {
  try {
    run();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
