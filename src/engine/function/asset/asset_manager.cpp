// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "asset_manager.hpp"
#ifdef GNEISS_ENABLE_CAORS_INPUT
#include "engine/function/input/caors_input_host.hpp"
#endif

namespace gneiss::engine {
struct asset_manager::input_state {
#ifdef GNEISS_ENABLE_CAORS_INPUT
  input_internal::caors_input_host host;
  input_state(asset_internal::virtual_file_system files, tasks::task_executor& executor)
      : host(std::move(files), executor) {}
#endif
};
asset_manager::asset_manager(asset_internal::virtual_file_system files,
                             render_internal::render_resource_service& resources)
    : files_(std::move(files)), render_(files_, cache_, resources), prefabs_(files_, cache_) {}
asset_manager::~asset_manager() { stop_input(); }
void asset_manager::attach_input_loader(tasks::task_executor& executor) {
#ifdef GNEISS_ENABLE_CAORS_INPUT
  if (!input_) {
    input_ = std::make_unique<input_state>(files_, executor);
  }
#else
  (void)executor;
#endif
}
result asset_manager::begin_input(std::string_view uri) noexcept {
#ifdef GNEISS_ENABLE_CAORS_INPUT
  return input_ ? input_->host.begin(uri) : result::not_ready;
#else
  (void)uri;
  return result::unsupported;
#endif
}
result asset_manager::input_status() const noexcept {
#ifdef GNEISS_ENABLE_CAORS_INPUT
  return input_ ? input_->host.status() : result::not_ready;
#else
  return result::unsupported;
#endif
}
result asset_manager::cancel_input() noexcept {
#ifdef GNEISS_ENABLE_CAORS_INPUT
  if (!input_) {
    return result::not_ready;
  }
  input_->host.cancel();
  return result::success;
#else
  return result::unsupported;
#endif
}
void asset_manager::advance_input(const std::function<result(input_internal::action_map)>& apply) {
#ifdef GNEISS_ENABLE_CAORS_INPUT
  if (input_) {
    input_->host.advance(apply);
  }
#else
  (void)apply;
#endif
}
void asset_manager::stop_input() noexcept { input_.reset(); }
}
