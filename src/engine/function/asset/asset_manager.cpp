// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "asset_manager.hpp"
#include "engine/function/input/caors_input_host.hpp"

namespace gneiss::engine {
struct asset_manager::input_state {
  input_internal::caors_input_host host;
  input_state(asset_internal::virtual_file_system files, tasks::task_executor& executor)
      : host(std::move(files), executor) {}
};
asset_manager::asset_manager(asset_internal::virtual_file_system files,
                             render_internal::render_resource_service& resources)
    : files_(std::move(files)), render_(files_, cache_, resources), prefabs_(files_, cache_) {}
asset_manager::~asset_manager() { stop_input(); }
void asset_manager::attach_input_loader(tasks::task_executor& executor) {
  if (!input_) {
    input_ = std::make_unique<input_state>(files_, executor);
  }
}
result asset_manager::begin_input(std::string_view uri) noexcept {
  return input_ ? input_->host.begin(uri) : result::not_ready;
}
result asset_manager::input_status() const noexcept {
  return input_ ? input_->host.status() : result::not_ready;
}
result asset_manager::cancel_input() noexcept {
  if (!input_) {
    return result::not_ready;
  }
  input_->host.cancel();
  return result::success;
}
void asset_manager::advance_input(const std::function<result(input_internal::action_map)>& apply) {
  if (input_) {
    input_->host.advance(apply);
  }
}
void asset_manager::stop_input() noexcept { input_.reset(); }
}
