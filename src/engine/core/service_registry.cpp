// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/service_registry.hpp"
#include "engine/core/error.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace gneiss::engine {

service_registry::~service_registry() noexcept { (void)shutdown(); }

engine::error service_registry::add(std::unique_ptr<service> value) noexcept {
  if (value == nullptr || is_initializing_or_initialized_ || find(value->kind()) != nullptr) {
    return engine::error::invalid_argument;
  }
  try {
    entries_.push_back({.value = std::move(value), .is_initialized = false});
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error service_registry::initialize() noexcept {
  if (is_initializing_or_initialized_) {
    return engine::error::invalid_state;
  }
  is_initializing_or_initialized_ = true;
  last_shutdown_leak_count_ = 0;

  try {
    initialization_order_.reserve(entries_.size());
  } catch (const std::bad_alloc&) {
    is_initializing_or_initialized_ = false;
    return engine::error::out_of_memory;
  } catch (...) {
    is_initializing_or_initialized_ = false;
    return engine::error::internal;
  }

  try {
    while (initialization_order_.size() < entries_.size()) {
      bool made_progress = false;
      for (std::size_t index = 0; index < entries_.size(); ++index) {
        auto& candidate = entries_[index];
        if (candidate.is_initialized || !dependencies_ready(*candidate.value)) {
          continue;
        }

        const auto result = candidate.value->initialize();
        if (result != engine::error::success) {
          // 即使初始化只完成了一部分，也由 Registry 统一调用 shutdown 回收。
          candidate.is_initialized = true;
          initialization_order_.push_back(index);
          rollback();
          return result;
        }
        candidate.is_initialized = true;
        initialization_order_.push_back(index);
        made_progress = true;
      }

      if (!made_progress) {
        rollback();
        return engine::error::dependency_failed;
      }
    }
  } catch (const std::bad_alloc&) {
    rollback();
    return engine::error::out_of_memory;
  } catch (...) {
    rollback();
    return engine::error::internal;
  }
  return engine::error::success;
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

} // namespace gneiss::engine
