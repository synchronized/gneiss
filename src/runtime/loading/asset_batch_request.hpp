// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "runtime/loading/resource_load_types.hpp"
#include <memory>
#include <optional>
namespace gneiss::runtime {
class resource_load_group;
/** 资产批次的独占观察句柄；仅所属线程使用，放弃即取消，不保活宿主。 */
class asset_batch_request final {
public:
  asset_batch_request() = default;
  GNEISS_API ~asset_batch_request();
  GNEISS_API asset_batch_request(asset_batch_request&&) noexcept;
  GNEISS_API asset_batch_request& operator=(asset_batch_request&&) noexcept;
  asset_batch_request(const asset_batch_request&) = delete;
  asset_batch_request& operator=(const asset_batch_request&) = delete;
  [[nodiscard]] GNEISS_API explicit operator bool() const noexcept;
  [[nodiscard]] GNEISS_API bool ready() const;
  [[nodiscard]] GNEISS_API std::optional<asset_load_progress> progress() const;
  [[nodiscard]] GNEISS_API engine::result<asset_load_completion> take();
  [[nodiscard]] GNEISS_API engine::error cancel();
  GNEISS_API void reset() noexcept;

private:
  friend class resource_load_group;
  asset_batch_request(std::weak_ptr<resource_load_group*> owner, std::uint64_t id)
      : owner_(std::move(owner)), id_(id) {}
  resource_load_group* owner() const noexcept;
  std::weak_ptr<resource_load_group*> owner_;
  std::uint64_t id_{};
};
}
