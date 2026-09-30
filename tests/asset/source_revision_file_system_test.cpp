// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/source_revision_file_system.h"

#include <algorithm>
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
  gneiss_result read_bounded(std::string_view /*path*/, std::size_t limit,
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

struct range_state {
  std::vector<std::byte> bytes = std::vector<std::byte>(200000U, std::byte{7});
  std::size_t largest_read{};
  std::size_t reads{};
  bool fail{};
};
class memory_source final : public read_source {
public:
  explicit memory_source(std::shared_ptr<range_state> state)
      : state_(std::move(state)), length_(state_->bytes.size()) {}
  [[nodiscard]] std::uint64_t size() const noexcept override { return length_; }
  [[nodiscard]] gneiss_result read_at(std::uint64_t offset,
                                      std::span<std::byte> output) const noexcept override {
    ++state_->reads;
    state_->largest_read = std::max(state_->largest_read, output.size());
    if (state_->fail || offset > state_->bytes.size() ||
        output.size() > state_->bytes.size() - offset) {
      return GNEISS_ERROR_IO;
    }
    std::ranges::copy(
        std::span{state_->bytes}.subspan(static_cast<std::size_t>(offset), output.size()),
        output.begin());
    return GNEISS_SUCCESS;
  }

private:
  std::shared_ptr<range_state> state_;
  std::uint64_t length_;
};
struct range_files final : file_system {
  std::shared_ptr<range_state> state = std::make_shared<range_state>();
  gneiss_result read(std::string_view /*path*/,
                     std::vector<std::byte>& /*bytes*/) const noexcept override {
    return GNEISS_ERROR_IO;
  }
  gneiss_result open_read(std::string_view /*path*/,
                          std::unique_ptr<read_source>& output) const noexcept override try {
    output = std::make_unique<memory_source>(state);
    return GNEISS_SUCCESS;
  } catch (...) {
    output.reset();
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
};

void run_ranges() {
  auto backend = std::make_shared<range_files>();
  virtual_file_system vfs;
  check(vfs.mount("asset://", backend) == GNEISS_SUCCESS);
  source_revision_file_system tracked(vfs);
  std::unique_ptr<read_source> source;
  check(tracked.open_read("large", source) == GNEISS_SUCCESS);
  check(tracked.source_count() == 1U && backend->state->reads == 4U);
  check(backend->state->largest_read == 65536U);
  check(tracked.verify({}) == GNEISS_SUCCESS);
  std::size_t polls{};
  const auto reads = backend->state->reads;
  check(tracked.verify([&polls] { return ++polls == 3U; }) == GNEISS_ERROR_INVALID_STATE);
  check(backend->state->reads == reads + 1U);
  backend->state->bytes.back() = std::byte{8};
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  check(tracked.open_read("large", source) == GNEISS_ERROR_INVALID_STATE && !source);
  backend->state->bytes.back() = std::byte{7};
  check(tracked.open_read("large", source) == GNEISS_SUCCESS);
  backend->state->bytes.push_back(std::byte{});
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  backend->state->bytes.pop_back();
  backend->state->fail = true;
  check(tracked.open_read("new", source) == GNEISS_ERROR_IO && !source);
  check(tracked.source_count() == 1U);
  check(tracked.verify({}) == GNEISS_ERROR_INVALID_STATE);
  backend->state->fail = false;
  check(tracked.verify({}) == GNEISS_SUCCESS);
}
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
    run_ranges();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
