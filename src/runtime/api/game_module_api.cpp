// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include <gneiss/engine/game_module.h>

#include "engine/api/log_validation.hpp"
#include "runtime/game/game_context_internal.hpp"

extern "C" gneiss_result gneiss_game_module_validate(const gneiss_game_module_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (desc == nullptr || desc->struct_size < GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE ||
        desc->abi_version != GNEISS_GAME_MODULE_ABI_VERSION_CURRENT || desc->module_id == nullptr ||
        desc->module_id_length == 0U || desc->initialize == nullptr ||
        desc->fixed_update == nullptr || desc->update == nullptr || desc->shutdown == nullptr ||
        desc->reserved[0] != 0U || desc->reserved[1] != 0U) {
      return gneiss::engine::result::invalid_argument;
    }
    return gneiss::engine::result::success;
  }());
}

extern "C" gneiss_result gneiss_game_context_get_world(gneiss_game_context context,
                                                       gneiss_world* out_world) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_world == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_world = 0U;
    gneiss::runtime::context_view view;
    const auto result = gneiss::runtime::query_context(context, view);
    if (result == gneiss::engine::result::success) {
      *out_world = view.world;
    }
    return result;
  }());
}

extern "C" gneiss_result gneiss_game_context_get_startup_root_entity(gneiss_game_context context,
                                                                     gneiss_entity_id* out_entity) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_entity == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_entity = 0U;
    gneiss::runtime::context_view view;
    const auto result = gneiss::runtime::query_context(context, view);
    if (result == gneiss::engine::result::success) {
      *out_entity = view.startup_root_entity;
    }
    return result;
  }());
}

extern "C" gneiss_result gneiss_game_context_find_action(gneiss_game_context context,
                                                         const char* name, uint64_t name_length,
                                                         gneiss_action* out_action) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    gneiss::runtime::context_view view;
    const auto result = gneiss::runtime::query_context(context, view);
    return result == gneiss::engine::result::success
               ? gneiss::engine::detail::from_c_result(gneiss_application_find_action(
                     view.application, name, name_length, out_action))
               : result;
  }());
}

extern "C" gneiss_result gneiss_game_context_get_action_state(gneiss_game_context context,
                                                              gneiss_action action,
                                                              gneiss_action_state* out_state) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    gneiss::runtime::context_view view;
    const auto result = gneiss::runtime::query_context(context, view);
    return result == gneiss::engine::result::success
               ? gneiss::engine::detail::from_c_result(
                     gneiss_application_get_action_state(view.application, action, out_state))
               : result;
  }());
}

extern "C" gneiss_result gneiss_game_context_request_exit(gneiss_game_context context) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    gneiss::runtime::context_view view;
    const auto result = gneiss::runtime::query_context(context, view);
    return result == gneiss::engine::result::success
               ? gneiss::engine::detail::from_c_result(
                     gneiss_application_request_exit(view.application))
               : result;
  }());
}

extern "C" gneiss_result gneiss_game_context_log(gneiss_game_context context,
                                                 const gneiss_log_message* message) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    const auto result = gneiss::engine::detail::validate_log_message(message);
    return result == gneiss::engine::result::success
               ? gneiss::runtime::submit_context_log(
                     context, gneiss::engine::detail::log_message_view(*message))
               : result;
  }());
}
