// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.h>

#include "engine/api/log_validation.hpp"
#include "engine/function/game/game_context_internal.hpp"

extern "C" gneiss_result gneiss_game_module_validate(const gneiss_game_module_desc* desc) {
  if (desc == nullptr || desc->struct_size < GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE ||
      desc->abi_version != GNEISS_GAME_MODULE_ABI_VERSION_CURRENT || desc->module_id == nullptr ||
      desc->module_id_length == 0U || desc->initialize == nullptr ||
      desc->fixed_update == nullptr || desc->update == nullptr || desc->shutdown == nullptr ||
      desc->reserved[0] != 0U || desc->reserved[1] != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return GNEISS_SUCCESS;
}

extern "C" gneiss_result gneiss_game_context_get_world(gneiss_game_context context,
                                                       gneiss_world* out_world) {
  if (out_world == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_world = 0U;
  gneiss::game_internal::context_view view;
  const auto result = gneiss::game_internal::query_context(context, view);
  if (result == GNEISS_SUCCESS) {
    *out_world = view.world;
  }
  return result;
}

extern "C" gneiss_result gneiss_game_context_get_startup_root_entity(gneiss_game_context context,
                                                                     gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = 0U;
  gneiss::game_internal::context_view view;
  const auto result = gneiss::game_internal::query_context(context, view);
  if (result == GNEISS_SUCCESS) {
    *out_entity = view.startup_root_entity;
  }
  return result;
}

extern "C" gneiss_result gneiss_game_context_find_action(gneiss_game_context context,
                                                         const char* name, uint64_t name_length,
                                                         gneiss_action* out_action) {
  gneiss::game_internal::context_view view;
  const auto result = gneiss::game_internal::query_context(context, view);
  return result == GNEISS_SUCCESS
             ? gneiss_application_find_action(view.application, name, name_length, out_action)
             : result;
}

extern "C" gneiss_result gneiss_game_context_get_action_state(gneiss_game_context context,
                                                              gneiss_action action,
                                                              gneiss_action_state* out_state) {
  gneiss::game_internal::context_view view;
  const auto result = gneiss::game_internal::query_context(context, view);
  return result == GNEISS_SUCCESS
             ? gneiss_application_get_action_state(view.application, action, out_state)
             : result;
}

extern "C" gneiss_result gneiss_game_context_request_exit(gneiss_game_context context) {
  gneiss::game_internal::context_view view;
  const auto result = gneiss::game_internal::query_context(context, view);
  return result == GNEISS_SUCCESS ? gneiss_application_request_exit(view.application) : result;
}

extern "C" gneiss_result gneiss_game_context_log(gneiss_game_context context,
                                                 const gneiss_log_message* message) {
  const auto result = gneiss::abi_internal::validate_log_message(message);
  return result == GNEISS_SUCCESS ? gneiss::game_internal::submit_context_log(context, *message)
                                  : result;
}
