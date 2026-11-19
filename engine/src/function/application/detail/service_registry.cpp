// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>

#include "function/application/detail/service_registry.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace gneiss {

service_registry::~service_registry() noexcept { (void)shutdown(); }

gneiss::error service_registry::add(std::unique_ptr<service> value) {
  if (value == nullptr || is_initializing_or_initialized_ || find(value->kind()) != nullptr) {
    return gneiss::error::invalid_argument;
  }
  {
    entries_.push_back({.value = std::move(value), .is_initialized = false});
    return gneiss::error::success;
  }
}

gneiss::error service_registry::initialize() {
  if (is_initializing_or_initialized_) {
    return gneiss::error::invalid_state;
  }
  is_initializing_or_initialized_ = true;
  last_shutdown_leak_count_ = 0;

  try {
    initialization_order_.reserve(entries_.size());
  } catch (...) {
    is_initializing_or_initialized_ = false;
    throw;
  }

  try {
    while (initialization_order_.size() < entries_.size()) {
      bool made_progress = false;
      for (std::size_t index = 0; index < entries_.size(); ++index) {
        auto& candidate = entries_[index];
        if (candidate.is_initialized || !dependencies_ready(*candidate.value)) {
          continue;
        }

        // 调用前登记，异常也必须回收当前服务的部分初始化状态。
        candidate.is_initialized = true;
        initialization_order_.push_back(index);
        const auto result = candidate.value->initialize();
        if (result != gneiss::error::success) {
          rollback();
          return result;
        }
        made_progress = true;
      }

      if (!made_progress) {
        rollback();
        return gneiss::error::dependency_failed;
      }
    }
  } catch (...) {
    rollback();
    throw;
  }
  return gneiss::error::success;
}

std::size_t service_registry::shutdown() noexcept {
  rollback();
  return last_shutdown_leak_count_;
}

bool service_registry::is_initialized(service_kind kind) const noexcept {
  const auto* found = find(kind);
  return found != nullptr && found->is_initialized;
}

service_registry::entry* service_registry::find(service_kind kind) noexcept {
  const auto found = std::ranges::find_if(
      entries_, [kind](const entry& candidate) { return candidate.value->kind() == kind; });
  return found == entries_.end() ? nullptr : &*found;
}

const service_registry::entry* service_registry::find(service_kind kind) const noexcept {
  const auto found = std::ranges::find_if(
      entries_, [kind](const entry& candidate) { return candidate.value->kind() == kind; });
  return found == entries_.end() ? nullptr : &*found;
}

bool service_registry::dependencies_ready(const service& value) const noexcept {
  return std::ranges::all_of(
      value.dependencies(), [this](service_kind dependency) { return is_initialized(dependency); });
}

void service_registry::rollback() noexcept {
  last_shutdown_leak_count_ = 0;
  for (auto iterator = initialization_order_.rbegin(); iterator != initialization_order_.rend();
       ++iterator) {
    auto& initialized = entries_[*iterator];
    last_shutdown_leak_count_ += initialized.value->live_resource_count();
    initialized.value->shutdown();
    initialized.is_initialized = false;
  }
  initialization_order_.clear();
  is_initializing_or_initialized_ = false;
}

} // namespace gneiss
