// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "core/service_registry.h"

#include <algorithm>
#include <new>
#include <utility>

namespace gneiss::core {

service_registry::~service_registry() noexcept { (void)shutdown(); }

gneiss_result service_registry::add(std::unique_ptr<service> value) noexcept {
  if (value == nullptr || is_initializing_or_initialized_ || find(value->kind()) != nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    entries_.push_back({.value = std::move(value), .is_initialized = false});
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result service_registry::initialize() noexcept {
  if (is_initializing_or_initialized_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  is_initializing_or_initialized_ = true;
  last_shutdown_leak_count_ = 0;

  try {
    initialization_order_.reserve(entries_.size());
  } catch (const std::bad_alloc&) {
    is_initializing_or_initialized_ = false;
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    is_initializing_or_initialized_ = false;
    return GNEISS_ERROR_INTERNAL;
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
        if (result != GNEISS_SUCCESS) {
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
        return GNEISS_ERROR_DEPENDENCY_FAILED;
      }
    }
  } catch (const std::bad_alloc&) {
    rollback();
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    rollback();
    return GNEISS_ERROR_INTERNAL;
  }
  return GNEISS_SUCCESS;
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

} // namespace gneiss::core
