// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/resource/vfs/asset_source.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace gneiss::engine;
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct counters {
  unsigned opens{}, reads{}, closes{};
  std::size_t maximum{}, size{150000};
  bool short_read{};
};
class read final : public read_operation {
public:
  explicit read(std::size_t size) : bytes_(size, std::byte{42}) {}
  gneiss::engine::result poll(std::span<const std::byte>& output) noexcept override {
    if (!polled_) {
      polled_ = true;
      return gneiss::engine::result::not_ready;
    }
    output = bytes_;
    return gneiss::engine::result::success;
  }

private:
  bool polled_{};
  std::vector<std::byte> bytes_;
};
class source final : public read_source {
public:
  explicit source(std::shared_ptr<counters> stats) : stats_(std::move(stats)) {}
  std::uint64_t size() const noexcept override { return stats_->size; }
  gneiss::engine::result read_at(std::uint64_t, std::span<std::byte>) const noexcept override {
    return gneiss::engine::result::internal;
  }
  gneiss::engine::result
  begin_read(std::uint64_t, std::size_t size,
             std::unique_ptr<read_operation>& output) const noexcept override {
    ++stats_->reads;
    stats_->maximum = std::max(stats_->maximum, size);
    try {
      output = std::make_unique<read>(size - (stats_->short_read ? 1U : 0U));
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  gneiss::engine::result begin_close() const noexcept override {
    ++stats_->closes;
    return gneiss::engine::result::success;
  }
  gneiss::engine::result poll_close() const noexcept override {
    if (!closed_) {
      closed_ = true;
      return gneiss::engine::result::not_ready;
    }
    return gneiss::engine::result::success;
  }

private:
  std::shared_ptr<counters> stats_;
  mutable bool closed_{};
};
class opening final : public open_read_operation {
public:
  explicit opening(std::shared_ptr<counters> stats) : stats_(std::move(stats)) {}
  gneiss::engine::result poll(std::unique_ptr<read_source>& output) noexcept override {
    if (!polled_) {
      polled_ = true;
      return gneiss::engine::result::not_ready;
    }
    try {
      output = std::make_unique<source>(stats_);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }

private:
  std::shared_ptr<counters> stats_;
  bool polled_{};
};
class files final : public file_system {
public:
  std::shared_ptr<counters> stats = std::make_shared<counters>();
  gneiss::engine::result
  begin_open_read(std::string_view,
                  std::unique_ptr<open_read_operation>& output) const noexcept override {
    ++stats->opens;
    try {
      output = std::make_unique<opening>(stats);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  gneiss::engine::result read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return gneiss::engine::result::internal;
  }
};
void run() {
  auto backend = std::make_shared<files>();
  virtual_file_system vfs;
  check(vfs.mount("asset://", backend) == gneiss::engine::result::success, "mount");
  auto finish = [](asset_source& operation) {
    for (unsigned i = 0; i != 64; ++i) {
      auto result = operation.advance();
      if (result != gneiss::engine::result::not_ready)
        return result;
    }
    throw std::runtime_error("source never completed");
  };
  asset_source operation(vfs, "asset://test", 150000);
  check(finish(operation) == gneiss::engine::result::success, "async source");
  check(operation.bytes().size() == 150000 && backend->stats->reads == 3 &&
            backend->stats->maximum == 65536 && backend->stats->closes == 1,
        "bounded chunks and close");
  asset_source oversized(vfs, "asset://test", 149999);
  check(finish(oversized) == gneiss::engine::result::invalid_argument && backend->stats->reads == 3,
        "size limit before allocation and reads");
  backend->stats->short_read = true;
  asset_source short_read(vfs, "asset://test", 150000);
  check(finish(short_read) == gneiss::engine::result::io, "short read rejected");
  asset_source zero(vfs, "asset://test", 0);
  check(finish(zero) == gneiss::engine::result::invalid_argument, "zero limit rejected");
}
}
int main() {
  try {
    run();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
