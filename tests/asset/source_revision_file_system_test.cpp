// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/source_revision_file_system.hpp"

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
void run_incremental() {
  auto backend = std::make_shared<range_files>();
  virtual_file_system files;
  check(files.mount("asset://", backend) == GNEISS_SUCCESS);
  source_revision_file_system tracked(files);
  std::unique_ptr<read_source> opened;
  check(tracked.open_read("large", opened) == GNEISS_SUCCESS);
  opened.reset();
  std::unique_ptr<source_revision_file_system::verification> cursor;
  check(tracked.begin_verification(cursor) == GNEISS_SUCCESS);
  bool complete = true;
  const auto initial_reads = backend->state->reads;
  check(cursor->advance(0U, {}, complete) == GNEISS_ERROR_INVALID_ARGUMENT && !complete);
  check(backend->state->reads == initial_reads);
  for (std::size_t step = 1U; step <= 4U; ++step) {
    check(cursor->advance(65536U, {}, complete) == GNEISS_SUCCESS);
    check(backend->state->reads == initial_reads + step);
    check(complete == (step == 4U));
  }
  check(cursor->completed_sources() == 1U);
  check(cursor->advance(65536U, {}, complete) == GNEISS_SUCCESS && complete);
  check(backend->state->reads == initial_reads + 4U);

  // 同一游标跨步检测未读部分变化；失败后不能恢复成成功或继续 I/O。
  check(tracked.begin_verification(cursor) == GNEISS_SUCCESS);
  check(cursor->advance(65536U, {}, complete) == GNEISS_SUCCESS && !complete);
  backend->state->bytes.back() = std::byte{8};
  check(cursor->advance(200000U, {}, complete) == GNEISS_ERROR_INVALID_STATE && !complete);
  const auto failed_reads = backend->state->reads;
  backend->state->bytes.back() = std::byte{7};
  check(cursor->advance(200000U, {}, complete) == GNEISS_ERROR_INVALID_STATE);
  check(backend->state->reads == failed_reads);
  check(tracked.begin_verification(cursor) == GNEISS_SUCCESS);
  check(cursor->advance(65536U, {}, complete) == GNEISS_SUCCESS && !complete);
  check(cursor->advance(65536U, [] { return true; }, complete) == GNEISS_ERROR_INVALID_STATE);
  check(!complete);
  check(cursor->advance(65536U, {}, complete) == GNEISS_ERROR_INVALID_STATE);
  // 重试使用新游标；游标持有来源，不借用已经销毁的版本账本。
  {
    source_revision_file_system temporary(files);
    check(temporary.open_read("large", opened) == GNEISS_SUCCESS);
    check(temporary.begin_verification(cursor) == GNEISS_SUCCESS);
  }
  check(cursor->advance(200000U, {}, complete) == GNEISS_SUCCESS && complete);
  backend->state->bytes.clear();
  source_revision_file_system empty(files);
  for (std::size_t index = 0U; index < 17U; ++index) {
    check(empty.open_read(std::to_string(index), opened) == GNEISS_SUCCESS);
  }
  check(empty.begin_verification(cursor) == GNEISS_SUCCESS);
  check(cursor->advance(1U, {}, complete) == GNEISS_SUCCESS && !complete);
  check(cursor->completed_sources() == 16U);
  check(cursor->advance(1U, {}, complete) == GNEISS_SUCCESS && complete);
}
void run_deferred_validation() {
  auto backend = std::make_shared<range_files>();
  virtual_file_system files;
  check(files.mount("asset://", backend) == GNEISS_SUCCESS);
  auto outer = std::make_shared<source_revision_file_system>(files);
  virtual_file_system nested;
  check(nested.mount("asset://", outer) == GNEISS_SUCCESS);
  std::unique_ptr<read_source> source;
  {
    source_revision_file_system inner(nested);
    check(inner.open_read_for_validation("large", source) == GNEISS_SUCCESS);
    check(backend->state->reads == 0U && outer->source_count() == 0U && inner.source_count() == 0U);
    std::vector<std::byte> bytes(static_cast<std::size_t>(source->size()));
    check(source->read_at(0U, bytes) == GNEISS_SUCCESS);
    check(source->complete_validation(gneiss::core::sha256(bytes)) == GNEISS_SUCCESS);
    check(backend->state->reads == 1U && outer->source_count() == 1U && inner.source_count() == 1U);
    std::unique_ptr<source_revision_file_system::verification> cursor;
    check(inner.begin_verification(cursor) == GNEISS_SUCCESS);
    bool complete{};
    check(cursor->advance(65536U, {}, complete) == GNEISS_SUCCESS && !complete);
    // 嵌套版本跟踪不能在一次分步读取前偷偷同步扫描整个来源。
    check(backend->state->reads == 2U);
  }
  // 版本账本销毁后读取对象仍独立拥有验证所需状态。
  check(source->complete_validation(gneiss::core::sha256(backend->state->bytes)) == GNEISS_SUCCESS);
  backend->state->bytes.back() = std::byte{9};
  check(source->complete_validation(gneiss::core::sha256(backend->state->bytes)) ==
        GNEISS_ERROR_INVALID_STATE);
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
    run_incremental();
    run_deferred_validation();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
