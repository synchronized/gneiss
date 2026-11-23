// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/logging/sink_connection.hpp>

#include "detail/service_state.hpp"
#include <stdexcept>
namespace gneiss::logging {
using detail::valid_level;
void sink_connection::disconnect() const {
  auto state = state_.lock();
  auto slot = slot_.lock();
  if (!state || !slot)
    return;
  {
    std::scoped_lock lock(state->mutex);
    slot->attached = false;
    std::erase(state->outputs, slot);
  }
}
bool sink_connection::connected() const {
  auto state = state_.lock();
  auto slot = slot_.lock();
  if (!state || !slot)
    return false;
  std::scoped_lock lock(state->mutex);
  return state->active && slot->attached;
}
void sink_connection::set_level(level value) const {
  if (!valid_level(value))
    throw std::invalid_argument("invalid log level");
  auto state = state_.lock();
  auto slot = slot_.lock();
  if (!state || !slot)
    return;
  std::scoped_lock lock(state->mutex);
  slot->threshold = value;
}
}
