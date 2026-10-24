// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/resource/vfs/source_revision_file_system.hpp"

#include <algorithm>
#include <cstdio>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss::engine::resource;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("源版本契约失败，行=" + std::to_string(where.line()));
  }
}
struct memory_files final : file_system {
  std::string text{"abc"};
  bool missing{};
  gneiss::engine::core::result read(std::string_view path,
                                    std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, 1024U, bytes);
  }
  gneiss::engine::core::result read_bounded(std::string_view /*path*/, std::size_t limit,
                                            std::vector<std::byte>& bytes) const noexcept override
      try {
    bytes.clear();
    if (missing) {
      return gneiss::engine::core::result::not_found;
    }
    if (text.size() > limit) {
      return gneiss::engine::core::result::invalid_argument;
    }
    const auto view = std::as_bytes(std::span{text});
    bytes.assign(view.begin(), view.end());
    return gneiss::engine::core::result::success;
  } catch (...) {
    return gneiss::engine::core::result::out_of_memory;
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
  [[nodiscard]] gneiss::engine::core::result
  read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    ++state_->reads;
    state_->largest_read = std::max(state_->largest_read, output.size());
    if (state_->fail || offset > state_->bytes.size() ||
        output.size() > state_->bytes.size() - offset) {
      return gneiss::engine::core::result::io;
    }
    std::ranges::copy(
        std::span{state_->bytes}.subspan(static_cast<std::size_t>(offset), output.size()),
        output.begin());
    return gneiss::engine::core::result::success;
  }

private:
  std::shared_ptr<range_state> state_;
  std::uint64_t length_;
};
struct range_files final : file_system {
  std::shared_ptr<range_state> state = std::make_shared<range_state>();
  gneiss::engine::core::result read(std::string_view /*path*/,
                                    std::vector<std::byte>& /*bytes*/) const noexcept override {
    return gneiss::engine::core::result::io;
  }
  gneiss::engine::core::result
  open_read(std::string_view /*path*/, std::unique_ptr<read_source>& output) const noexcept override
      try {
    output = std::make_unique<memory_source>(state);
    return gneiss::engine::core::result::success;
  } catch (...) {
    output.reset();
    return gneiss::engine::core::result::out_of_memory;
  }
};

struct opening_state {
  std::shared_ptr<range_state> contents = std::make_shared<range_state>();
  bool ready{};
  bool fail{};
  unsigned live{};
  unsigned synchronous_reads{};
  gneiss::engine::core::result error{gneiss::engine::core::result::io};
  std::string path;
};
struct opening_files final : file_system {
  std::shared_ptr<opening_state> state = std::make_shared<opening_state>();
  gneiss::engine::core::result read(std::string_view,
                                    std::vector<std::byte>&) const noexcept override {
    ++state->synchronous_reads;
    return gneiss::engine::core::result::unsupported;
  }
  gneiss::engine::core::result begin_open_read_for_validation(
      std::string_view path, std::unique_ptr<open_read_operation>& output) const noexcept override {
    output.reset();
    try {
      class request final : public open_read_operation {
      public:
        explicit request(std::shared_ptr<opening_state> state) : state_(std::move(state)) {
          ++state_->live;
        }
        ~request() override { --state_->live; }
        gneiss::engine::core::result poll(std::unique_ptr<read_source>& source) noexcept override {
          source.reset();
          if (taken_) {
            return gneiss::engine::core::result::invalid_state;
          }
          if (!state_->ready) {
            return gneiss::engine::core::result::not_ready;
          }
          if (state_->fail) {
            return state_->error;
          }
          try {
            source = std::make_unique<memory_source>(state_->contents);
            taken_ = true;
            return gneiss::engine::core::result::success;
          } catch (...) {
            return gneiss::engine::core::result::out_of_memory;
          }
        }

      private:
        std::shared_ptr<opening_state> state_;
        bool taken_{};
      };
      state->path = path;
      output = std::make_unique<request>(state);
      return gneiss::engine::core::result::success;
    } catch (...) {
      return gneiss::engine::core::result::out_of_memory;
    }
  }
};

void run_async_open() {
  auto backend = std::make_shared<opening_files>();
  const auto state = backend->state;
  std::unique_ptr<open_read_operation> request;
  std::unique_ptr<open_read_operation> conflict;
  std::unique_ptr<open_read_operation> failure;
  std::unique_ptr<read_source> source;
  {
    virtual_file_system files;
    check(files.begin_open_read_for_validation("asset://missing", request) ==
          gneiss::engine::core::result::not_found);
    check(files.mount("asset://", std::make_shared<range_files>()) ==
          gneiss::engine::core::result::success);
    check(files.begin_open_read_for_validation("asset://legacy", request) ==
          gneiss::engine::core::result::unsupported);
    check(files.mount("asset://nested/", backend) == gneiss::engine::core::result::success);
    source_revision_file_system tracked(files);
    check(tracked.begin_open_read_for_validation("nested/file", request) ==
          gneiss::engine::core::result::success);
    check(state->path == "file" && state->contents->reads == 0U && tracked.source_count() == 0U);
    check(request->poll(source) == gneiss::engine::core::result::not_ready && !source);
    check(tracked.begin_open_read_for_validation("nested/file", conflict) ==
          gneiss::engine::core::result::success);
    check(tracked.begin_open_read_for_validation("nested/error", failure) ==
          gneiss::engine::core::result::success);
    std::unique_ptr<open_read_operation> discarded;
    check(tracked.begin_open_read_for_validation("nested/abandoned", discarded) ==
          gneiss::engine::core::result::success);
    discarded.reset();
    check(state->live == 3U);
    check(files.begin_open_read_for_validation("asset://../invalid", discarded) ==
          gneiss::engine::core::result::invalid_argument);
  }
  backend.reset();
  // 请求不借用已销毁的包装或挂载表；取得来源后仍共享原版本账本。
  state->ready = true;
  check(request->poll(source) == gneiss::engine::core::result::success && source &&
        state->live == 2U);
  check(state->contents->reads == 0U);
  const auto digest = gneiss::engine::core::sha256(state->contents->bytes);
  check(source->complete_validation(digest) == gneiss::engine::core::result::success);
  std::unique_ptr<read_source> second;
  check(conflict->poll(second) == gneiss::engine::core::result::success && second);
  auto changed = digest;
  changed[0] ^= std::byte{1};
  check(second->complete_validation(changed) == gneiss::engine::core::result::invalid_state);
  check(request->poll(second) == gneiss::engine::core::result::invalid_state && !second);
  state->fail = true;
  check(failure->poll(second) == gneiss::engine::core::result::io && !second);
  state->fail = false;
  check(failure->poll(second) == gneiss::engine::core::result::io && !second);
  failure.reset();
  check(state->live == 0U);
}

void run_async_verification() {
  auto backend = std::make_shared<opening_files>();
  auto state = backend->state;
  virtual_file_system files;
  check(files.mount("asset://", backend) == gneiss::engine::core::result::success);
  source_revision_file_system tracked(files);
  std::unique_ptr<open_read_operation> opened;
  std::unique_ptr<read_source> source;
  check(tracked.begin_open_read_for_validation("item", opened) ==
        gneiss::engine::core::result::success);
  state->ready = true;
  check(opened->poll(source) == gneiss::engine::core::result::success);
  check(source->complete_validation(gneiss::engine::core::sha256(state->contents->bytes)) ==
        gneiss::engine::core::result::success);
  source.reset();
  state->ready = false;
  std::unique_ptr<source_revision_file_system::verification> cursor;
  check(tracked.begin_verification(cursor) == gneiss::engine::core::result::success);
  bool complete = true;
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success &&
        !complete);
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success &&
        !complete);
  check(state->live == 1U && state->contents->reads == 0U);
  check(cursor->advance(
            65536U, [] { return true; }, complete) == gneiss::engine::core::result::invalid_state);
  check(state->live == 0U);
  state->ready = true; // 已取消请求不能在迟到后继续登记或读取。
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::invalid_state &&
        !complete);
  check(state->contents->reads == 0U);
  check(tracked.begin_verification(cursor) == gneiss::engine::core::result::success);
  for (unsigned i = 0; i < 8U && !complete; ++i) {
    check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success);
  }
  check(complete && cursor->completed_sources() == 1U && state->live == 0U);
  state->fail = true;
  state->error = gneiss::engine::core::result::unsupported;
  check(tracked.begin_verification(cursor) == gneiss::engine::core::result::success);
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::invalid_state &&
        !complete);
  // 已接受请求的失败不能偷偷转为同步整文件读取。
  check(state->synchronous_reads == 0U && state->live == 0U);
}

void run_ranges() {
  auto backend = std::make_shared<range_files>();
  virtual_file_system vfs;
  check(vfs.mount("asset://", backend) == gneiss::engine::core::result::success);
  source_revision_file_system tracked(vfs);
  std::unique_ptr<read_source> source;
  check(tracked.open_read("large", source) == gneiss::engine::core::result::success);
  check(tracked.source_count() == 1U && backend->state->reads == 4U);
  check(backend->state->largest_read == 65536U);
  check(tracked.verify({}) == gneiss::engine::core::result::success);
  std::size_t polls{};
  const auto reads = backend->state->reads;
  check(tracked.verify([&polls] { return ++polls == 3U; }) ==
        gneiss::engine::core::result::invalid_state);
  check(backend->state->reads == reads + 1U);
  backend->state->bytes.back() = std::byte{8};
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  check(tracked.open_read("large", source) == gneiss::engine::core::result::invalid_state &&
        !source);
  backend->state->bytes.back() = std::byte{7};
  check(tracked.open_read("large", source) == gneiss::engine::core::result::success);
  backend->state->bytes.push_back(std::byte{});
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  backend->state->bytes.pop_back();
  backend->state->fail = true;
  check(tracked.open_read("new", source) == gneiss::engine::core::result::io && !source);
  check(tracked.source_count() == 1U);
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  backend->state->fail = false;
  check(tracked.verify({}) == gneiss::engine::core::result::success);
}
void run_incremental() {
  auto backend = std::make_shared<range_files>();
  virtual_file_system files;
  check(files.mount("asset://", backend) == gneiss::engine::core::result::success);
  source_revision_file_system tracked(files);
  std::unique_ptr<read_source> opened;
  check(tracked.open_read("large", opened) == gneiss::engine::core::result::success);
  opened.reset();
  std::unique_ptr<source_revision_file_system::verification> cursor;
  check(tracked.begin_verification(cursor) == gneiss::engine::core::result::success);
  bool complete = true;
  const auto initial_reads = backend->state->reads;
  check(cursor->advance(0U, {}, complete) == gneiss::engine::core::result::invalid_argument &&
        !complete);
  check(backend->state->reads == initial_reads);
  for (std::size_t step = 1U; step <= 4U; ++step) {
    check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success);
    check(backend->state->reads == initial_reads + step);
    check(complete == (step == 4U));
  }
  check(cursor->completed_sources() == 1U);
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success && complete);
  check(backend->state->reads == initial_reads + 4U);

  // 同一游标跨步检测未读部分变化；失败后不能恢复成成功或继续 I/O。
  check(tracked.begin_verification(cursor) == gneiss::engine::core::result::success);
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success &&
        !complete);
  backend->state->bytes.back() = std::byte{8};
  check(cursor->advance(200000U, {}, complete) == gneiss::engine::core::result::invalid_state &&
        !complete);
  const auto failed_reads = backend->state->reads;
  backend->state->bytes.back() = std::byte{7};
  check(cursor->advance(200000U, {}, complete) == gneiss::engine::core::result::invalid_state);
  check(backend->state->reads == failed_reads);
  check(tracked.begin_verification(cursor) == gneiss::engine::core::result::success);
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success &&
        !complete);
  check(cursor->advance(
            65536U, [] { return true; }, complete) == gneiss::engine::core::result::invalid_state);
  check(!complete);
  check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::invalid_state);
  // 重试使用新游标；游标持有来源，不借用已经销毁的版本账本。
  {
    source_revision_file_system temporary(files);
    check(temporary.open_read("large", opened) == gneiss::engine::core::result::success);
    check(temporary.begin_verification(cursor) == gneiss::engine::core::result::success);
  }
  check(cursor->advance(200000U, {}, complete) == gneiss::engine::core::result::success &&
        complete);
  backend->state->bytes.clear();
  source_revision_file_system empty(files);
  for (std::size_t index = 0U; index < 17U; ++index) {
    check(empty.open_read(std::to_string(index), opened) == gneiss::engine::core::result::success);
  }
  check(empty.begin_verification(cursor) == gneiss::engine::core::result::success);
  check(cursor->advance(1U, {}, complete) == gneiss::engine::core::result::success && !complete);
  check(cursor->completed_sources() == 16U);
  check(cursor->advance(1U, {}, complete) == gneiss::engine::core::result::success && complete);
}
void run_deferred_validation() {
  auto backend = std::make_shared<range_files>();
  virtual_file_system files;
  check(files.mount("asset://", backend) == gneiss::engine::core::result::success);
  auto outer = std::make_shared<source_revision_file_system>(files);
  virtual_file_system nested;
  check(nested.mount("asset://", outer) == gneiss::engine::core::result::success);
  std::unique_ptr<read_source> source;
  {
    source_revision_file_system inner(nested);
    check(inner.open_read_for_validation("large", source) == gneiss::engine::core::result::success);
    check(backend->state->reads == 0U && outer->source_count() == 0U && inner.source_count() == 0U);
    std::vector<std::byte> bytes(static_cast<std::size_t>(source->size()));
    check(source->read_at(0U, bytes) == gneiss::engine::core::result::success);
    check(source->complete_validation(gneiss::engine::core::sha256(bytes)) ==
          gneiss::engine::core::result::success);
    check(backend->state->reads == 1U && outer->source_count() == 1U && inner.source_count() == 1U);
    std::unique_ptr<source_revision_file_system::verification> cursor;
    check(inner.begin_verification(cursor) == gneiss::engine::core::result::success);
    bool complete{};
    check(cursor->advance(65536U, {}, complete) == gneiss::engine::core::result::success &&
          !complete);
    // 嵌套版本跟踪不能在一次分步读取前偷偷同步扫描整个来源。
    check(backend->state->reads == 2U);
  }
  // 版本账本销毁后读取对象仍独立拥有验证所需状态。
  check(source->complete_validation(gneiss::engine::core::sha256(backend->state->bytes)) ==
        gneiss::engine::core::result::success);
  backend->state->bytes.back() = std::byte{9};
  check(source->complete_validation(gneiss::engine::core::sha256(backend->state->bytes)) ==
        gneiss::engine::core::result::invalid_state);
}
void run() {
  auto memory = std::make_shared<memory_files>();
  virtual_file_system files;
  check(files.mount("asset://", memory) == gneiss::engine::core::result::success);
  source_revision_file_system tracked(files);
  std::vector<std::byte> bytes;
  check(tracked.read_bounded("scene.json", 3U, bytes) == gneiss::engine::core::result::success);
  check(tracked.read("scene.json", bytes) == gneiss::engine::core::result::success);
  check(tracked.source_count() == 1U);
  check(tracked.verify({}) == gneiss::engine::core::result::success);
  check(tracked.verify([] { return true; }) == gneiss::engine::core::result::invalid_state);
  memory->text = "abd";
  check(tracked.read("scene.json", bytes) == gneiss::engine::core::result::invalid_state &&
        bytes.empty());
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  // 后续批次读新 URI 不得覆盖先前 URI 的版本身份。
  check(tracked.read("texture.png", bytes) == gneiss::engine::core::result::success);
  check(tracked.source_count() == 2U);
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  memory->text = "abcd";
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  memory->text = "ab";
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  memory->missing = true;
  check(tracked.verify({}) == gneiss::engine::core::result::invalid_state);
  memory->missing = false;
  memory->text = "abc";
  // 新会话独立建账；原始 VFS 保持可更新，不受旧会话约束。
  source_revision_file_system retry(files);
  check(retry.read("scene.json", bytes) == gneiss::engine::core::result::success);
  check(retry.verify({}) == gneiss::engine::core::result::success);
  memory->text = "new";
  check(files.read("asset://scene.json", bytes) == gneiss::engine::core::result::success);
  check(retry.read_bounded("other", 2U, bytes) == gneiss::engine::core::result::invalid_argument);
  check(retry.source_count() == 1U);
}
void run_identity_and_revision() {
  auto memory = std::make_shared<memory_files>();
  virtual_file_system files;
  check(files.mount("asset://", memory) == gneiss::engine::core::result::success);
  auto overlay = std::make_shared<memory_files>();
  overlay->text = "override";
  check(files.mount("asset://nested/", overlay) == gneiss::engine::core::result::success);
  auto tracked = std::make_shared<source_revision_file_system>(files);
  virtual_file_system wrapped;
  check(wrapped.mount("asset://", tracked) == gneiss::engine::core::result::success);
  auto nested = std::make_shared<source_revision_file_system>(wrapped);
  virtual_file_system twice;
  check(twice.mount("asset://", nested) == gneiss::engine::core::result::success);
  for (const auto uri : {"asset://scene.json", "asset://nested/scene.json"}) {
    asset_identity original;
    asset_identity forwarded;
    check(files.identify(uri, 1U, original) == gneiss::engine::core::result::success);
    check(twice.identify(uri, 1U, forwarded) == gneiss::engine::core::result::success &&
          original == forwarded);
  }
  check(tracked->source_count() == 0U && nested->source_count() == 0U);
  source_revision_file_system::source_revision revision;
  check(tracked->observed_revision("scene.json", revision) ==
        gneiss::engine::core::result::not_ready);
  std::vector<std::byte> bytes;
  check(wrapped.read("asset://scene.json", bytes) == gneiss::engine::core::result::success);
  check(tracked->observed_revision("scene.json", revision) ==
        gneiss::engine::core::result::success);
  check(revision.bytes == 3U && revision.digest == gneiss::engine::core::sha256(bytes));
  const auto old = revision;
  memory->text = "changed";
  // 查询只读账本，不能把旧摘要当作文件尚未改变的证明。
  check(tracked->observed_revision("scene.json", revision) ==
            gneiss::engine::core::result::success &&
        revision.digest == old.digest);
  check(tracked->verify({}) == gneiss::engine::core::result::invalid_state);
  check(tracked->observed_revision("../bad", revision) ==
            gneiss::engine::core::result::invalid_argument &&
        revision.bytes == 0U && revision.digest == gneiss::engine::core::sha256_digest{});
  auto missing = std::make_shared<source_revision_file_system>(virtual_file_system{});
  virtual_file_system missing_wrapper;
  check(missing_wrapper.mount("asset://", missing) == gneiss::engine::core::result::success);
  asset_identity output;
  check(missing_wrapper.identify("asset://scene.json", 1U, output) ==
            gneiss::engine::core::result::not_found &&
        !output.source);
}
} // namespace
int main() {
  try {
    run_identity_and_revision();
    run();
    run_ranges();
    run_async_open();
    run_async_verification();
    run_incremental();
    run_deferred_validation();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
