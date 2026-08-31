// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "game_module_session.h"

#include <cstring>

namespace gneiss {

game_module_session::~game_module_session() {
  if (initialized_ && !shutdown_called_) {
    (void)shutdown();
  }
}

result game_module_session::load(const std::filesystem::path& path) noexcept {
  if (library_.is_open()) {
    return result::invalid_state;
  }
  auto load_result = library_.open(path);
  if (load_result != result::success) {
    return load_result;
  }

  void* symbol{};
  load_result = library_.find_symbol(GNEISS_GAME_MODULE_QUERY_SYMBOL, &symbol);
  if (load_result != result::success) {
    library_.close();
    return load_result;
  }
  gneiss_game_module_query_fn query{};
  static_assert(sizeof(query) == sizeof(symbol));
  std::memcpy(&query, &symbol, sizeof(query));
  desc_ = {};
  desc_.struct_size = sizeof(desc_);
  load_result = from_native(query(GNEISS_GAME_MODULE_ABI_VERSION_CURRENT, &desc_));
  if (load_result == result::success) {
    load_result = from_native(gneiss_game_module_validate(&desc_));
  }
  if (load_result != result::success) {
    desc_ = {};
    library_.close();
  }
  return load_result;
}

result game_module_session::initialize(gneiss_game_context context) noexcept {
  if (!library_.is_open() || initialized_ || shutdown_called_ ||
      context == GNEISS_NULL_GAME_CONTEXT) {
    return result::invalid_state;
  }
  void* state{};
  const auto initialize_result = from_native(desc_.initialize(context, &state));
  if (initialize_result != result::success) {
    return initialize_result;
  }
  if (state == nullptr) {
    return result::initialization_failed;
  }
  context_ = context;
  module_state_ = state;
  initialized_ = true;
  return result::success;
}

result game_module_session::fixed_update(const gneiss_game_update_time& time) noexcept {
  if (!initialized_ || shutdown_called_) {
    return result::invalid_state;
  }
  return from_native(desc_.fixed_update(context_, module_state_, &time));
}

result game_module_session::update(const gneiss_game_update_time& time) noexcept {
  if (!initialized_ || shutdown_called_) {
    return result::invalid_state;
  }
  return from_native(desc_.update(context_, module_state_, &time));
}

result game_module_session::shutdown() noexcept {
  if (!initialized_ || shutdown_called_) {
    return result::invalid_state;
  }
  shutdown_called_ = true;
  const auto shutdown_result = from_native(desc_.shutdown(context_, module_state_));
  module_state_ = nullptr;
  initialized_ = false;
  return shutdown_result;
}

bool game_module_session::is_loaded() const noexcept { return library_.is_open(); }
bool game_module_session::is_initialized() const noexcept { return initialized_; }
std::string_view game_module_session::module_id() const noexcept {
  return is_loaded() ? std::string_view(desc_.module_id, desc_.module_id_length)
                     : std::string_view{};
}

} // namespace gneiss
