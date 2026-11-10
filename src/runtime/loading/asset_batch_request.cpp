// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/asset_batch_request.hpp"
#include "runtime/loading/resource_load_group.hpp"
#include <utility>
namespace gneiss::runtime {
resource_load_group* asset_batch_request::owner() const noexcept {
  auto token = owner_.lock();
  auto* group = token ? *token : nullptr;
  return group && std::this_thread::get_id() == group->owner_ && !group->stopping_ ? group
                                                                                   : nullptr;
}
asset_batch_request::~asset_batch_request() { reset(); }
asset_batch_request::asset_batch_request(asset_batch_request&& other) noexcept
    : owner_(std::move(other.owner_)), id_(std::exchange(other.id_, 0)) {}
asset_batch_request& asset_batch_request::operator=(asset_batch_request&& other) noexcept {
  if (this != &other) {
    reset();
    owner_ = std::move(other.owner_);
    id_ = std::exchange(other.id_, 0);
  }
  return *this;
}
asset_batch_request::operator bool() const noexcept {
  auto* group = owner();
  return group && group->owns(id_);
}
void asset_batch_request::reset() noexcept {
  if (auto* group = owner())
    (void)group->discard(id_);
  id_ = 0;
  owner_.reset();
}
bool asset_batch_request::ready() const {
  auto* group = owner();
  return group && group->owns(id_) && group->completion_->state != asset_load_state::preparing;
}
std::optional<asset_load_progress> asset_batch_request::progress() const {
  auto* group = owner();
  if (!group || !group->owns(id_))
    return {};
  asset_load_progress value;
  return group->progress(value) ? std::optional{value} : std::nullopt;
}
engine::result<asset_load_completion> asset_batch_request::take() {
  auto* group = owner();
  if (!group || !group->owns(id_))
    return engine::error::invalid_state;
  asset_load_completion value;
  if (!group->take(value))
    return engine::error::not_ready;
  reset();
  return value;
}
engine::error asset_batch_request::cancel() {
  auto* group = owner();
  return group && group->owns(id_) && group->cancel(id_) ? engine::error::success
                                                         : engine::error::invalid_state;
}

}
