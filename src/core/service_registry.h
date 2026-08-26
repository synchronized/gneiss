// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_SERVICE_REGISTRY_H_
#define GNEISS_CORE_SERVICE_REGISTRY_H_

#include <gneiss/core/result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gneiss::core {

enum class service_kind : std::uint8_t {
  platform = 1,
  resource = 2,
  render = 3,
};

class service {
public:
  service() = default;
  virtual ~service() = default;

  service(const service&) = delete;
  service& operator=(const service&) = delete;
  service(service&&) = delete;
  service& operator=(service&&) = delete;

  [[nodiscard]] virtual service_kind kind() const noexcept = 0;
  [[nodiscard]] virtual std::span<const service_kind> dependencies() const noexcept = 0;
  [[nodiscard]] virtual gneiss_result initialize() noexcept = 0;
  virtual void shutdown() noexcept = 0;
  [[nodiscard]] virtual std::size_t live_resource_count() const noexcept = 0;
};

/** 按显式依赖顺序初始化 Service，并在失败或关闭时逆序清理。 */
class service_registry final {
public:
  service_registry() = default;
  ~service_registry() noexcept;

  service_registry(const service_registry&) = delete;
  service_registry& operator=(const service_registry&) = delete;
  service_registry(service_registry&&) = delete;
  service_registry& operator=(service_registry&&) = delete;

  [[nodiscard]] gneiss_result add(std::unique_ptr<service> value) noexcept;
  [[nodiscard]] gneiss_result initialize() noexcept;
  [[nodiscard]] std::size_t shutdown() noexcept;

  [[nodiscard]] bool is_initialized(service_kind kind) const noexcept;
  [[nodiscard]] std::size_t last_shutdown_leak_count() const noexcept {
    return last_shutdown_leak_count_;
  }

private:
  struct entry {
    std::unique_ptr<service> value;
    bool is_initialized = false;
  };

  [[nodiscard]] entry* find(service_kind kind) noexcept;
  [[nodiscard]] const entry* find(service_kind kind) const noexcept;
  [[nodiscard]] bool dependencies_ready(const service& value) const noexcept;
  void rollback() noexcept;

  std::vector<entry> entries_;
  std::vector<std::size_t> initialization_order_;
  std::size_t last_shutdown_leak_count_ = 0;
  bool is_initializing_or_initialized_ = false;
};

} // namespace gneiss::core

#endif
