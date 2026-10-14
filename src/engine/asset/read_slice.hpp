// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_ASSET_READ_SLICE_HPP
#define GNEISS_ENGINE_ASSET_READ_SLICE_HPP

#include "engine/asset/read_source.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace gneiss::asset_internal {

/** 顺序消费区间请求；返回视图只在下次 take 或销毁前有效。
 * 异步缓冲跨步骤拥有，调用方减小预算时只消费对应部分；同步回退每次最多 64 KiB。 */
class read_slice final {
public:
  struct range {
    std::uint64_t offset;
    std::size_t size;
  };
  void prime(std::unique_ptr<read_operation> operation, range request) noexcept {
    operation_ = std::move(operation);
    offset_ = request.offset;
    size_ = request.size;
    consumed_ = 0U;
  }
  [[nodiscard]] gneiss_result take(const read_source& source, range request,
                                   std::span<const std::byte>& output, bool deferred = true,
                                   std::chrono::milliseconds wait_budget = {}) {
    const auto offset = request.offset;
    const auto limit = request.size;
    output = {};
    if (operation_ && consumed_ == size_) {
      operation_.reset();
    }
    if (!operation_) {
      offset_ = offset;
      size_ = std::min(limit, std::size_t{16U} * 1024U * 1024U);
      consumed_ = 0U;
      const auto started =
          deferred ? source.begin_read(offset, size_, operation_) : GNEISS_ERROR_UNSUPPORTED;
      if (started == GNEISS_ERROR_UNSUPPORTED) {
        const auto target = std::span{fallback_}.first(std::min(limit, fallback_.size()));
        const auto read = source.read_at(offset, target);
        if (read == GNEISS_SUCCESS) {
          output = target;
        }
        return read;
      }
      if (started != GNEISS_SUCCESS) {
        return started;
      }
      if (!operation_) {
        return GNEISS_ERROR_INTERNAL;
      }
    }
    if (offset != offset_ + consumed_) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    std::span<const std::byte> ready;
    auto polled = operation_->poll(ready);
    if (polled == GNEISS_ERROR_NOT_READY && wait_budget > std::chrono::milliseconds::zero()) {
      polled = operation_->wait_for(wait_budget, ready);
    }
    if (polled != GNEISS_SUCCESS) {
      return polled;
    }
    if (ready.size() != size_) {
      return GNEISS_ERROR_IO;
    }
    output = ready.subspan(consumed_, std::min(limit, size_ - consumed_));
    consumed_ += output.size();
    return GNEISS_SUCCESS;
  }

private:
  std::unique_ptr<read_operation> operation_;
  std::array<std::byte, std::size_t{64U} * 1024U> fallback_{};
  std::uint64_t offset_{};
  std::size_t size_{};
  std::size_t consumed_{};
};

} // namespace gneiss::asset_internal

#endif
