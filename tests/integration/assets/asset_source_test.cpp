// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/vfs/asset_source.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace gneiss::engine::resource;
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct counters {
  unsigned opens{}, reads{}, closes{}, verified{};
  std::size_t maximum{}, size{150000};
  bool short_read{};
};
class read final : public read_operation {
public:
  explicit read(std::size_t size) : bytes_(size, std::byte{42}) {}
  gneiss_result poll(std::span<const std::byte>& output) noexcept override {
    if (!polled_) {
      polled_ = true;
      return GNEISS_ERROR_NOT_READY;
    }
    output = bytes_;
    return GNEISS_SUCCESS;
  }

private:
  bool polled_{};
  std::vector<std::byte> bytes_;
};
class source final : public read_source {
public:
  explicit source(std::shared_ptr<counters> stats) : stats_(std::move(stats)) {}
  std::uint64_t size() const noexcept override { return stats_->size; }
  gneiss_result read_at(std::uint64_t, std::span<std::byte>) const noexcept override {
    return GNEISS_ERROR_INTERNAL;
  }
  gneiss_result begin_read(std::uint64_t, std::size_t size,
                           std::unique_ptr<read_operation>& output) const noexcept override {
    ++stats_->reads;
    stats_->maximum = std::max(stats_->maximum, size);
    try {
      output = std::make_unique<read>(size - (stats_->short_read ? 1U : 0U));
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
  gneiss_result complete_validation(const engine::core::sha256_digest&) const noexcept override {
    ++stats_->verified;
    return GNEISS_SUCCESS;
  }
  gneiss_result begin_close() const noexcept override {
    ++stats_->closes;
    return GNEISS_SUCCESS;
  }
  gneiss_result poll_close() const noexcept override {
    if (!closed_) {
      closed_ = true;
      return GNEISS_ERROR_NOT_READY;
    }
    return GNEISS_SUCCESS;
  }

private:
  std::shared_ptr<counters> stats_;
  mutable bool closed_{};
};
class opening final : public open_read_operation {
public:
  explicit opening(std::shared_ptr<counters> stats) : stats_(std::move(stats)) {}
  gneiss_result poll(std::unique_ptr<read_source>& output) noexcept override {
    if (!polled_) {
      polled_ = true;
      return GNEISS_ERROR_NOT_READY;
    }
    try {
      output = std::make_unique<source>(stats_);
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }

private:
  std::shared_ptr<counters> stats_;
  bool polled_{};
};
class files final : public file_system {
public:
  std::shared_ptr<counters> stats = std::make_shared<counters>();
  gneiss_result begin_open_read_for_validation(
      std::string_view, std::unique_ptr<open_read_operation>& output) const noexcept override {
    ++stats->opens;
    try {
      output = std::make_unique<opening>(stats);
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
  gneiss_result read(std::string_view, std::vector<std::byte>&) const noexcept override {
    return GNEISS_ERROR_INTERNAL;
  }
};
void run() {
  auto backend = std::make_shared<files>();
  virtual_file_system vfs;
  check(vfs.mount("asset://", backend) == GNEISS_SUCCESS, "mount");
  auto finish = [](asset_source& operation) {
    for (unsigned i = 0; i != 64; ++i) {
      auto result = operation.advance();
      if (result != result::not_ready)
        return result;
    }
    throw std::runtime_error("source never completed");
  };
  asset_source operation(vfs, "asset://test", 150000);
  check(finish(operation) == result::success, "async source");
  check(operation.bytes().size() == 150000 && backend->stats->reads == 3 &&
            backend->stats->maximum == 65536 && backend->stats->verified == 1 &&
            backend->stats->closes == 1,
        "bounded chunks, verification and close");
  asset_source oversized(vfs, "asset://test", 149999);
  check(finish(oversized) == result::invalid_argument && backend->stats->reads == 3,
        "size limit before allocation and reads");
  backend->stats->short_read = true;
  asset_source short_read(vfs, "asset://test", 150000);
  check(finish(short_read) == result::io, "short read rejected");
  asset_source zero(vfs, "asset://test", 0);
  check(finish(zero) == result::invalid_argument, "zero limit rejected");
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
