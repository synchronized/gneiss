// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include <gneiss/engine/game_module.h>

#include "engine/api/log_validation.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include "runtime/game/game_context_internal.hpp"
#include <algorithm>
#include <limits>

namespace {
template <typename Function>
gneiss::engine::result with_application(gneiss_game_context context,
                                        Function&& operation) noexcept {
  gneiss::runtime::context_view view;
  auto status = gneiss::runtime::query_context(context, view);
  if (status != gneiss::engine::result::success)
    return status;
  try {
    return operation(*view.application);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
}

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
                                                         gneiss_action* output) {
  return with_application(context,
                          [&](auto& application) {
                            if (!output || !name || name_length == 0 ||
                                name_length > std::numeric_limits<std::size_t>::max())
                              return gneiss::engine::result::invalid_argument;
                            *output = GNEISS_NULL_ACTION;
                            gneiss::engine::action_id action;
                            const auto status = application.find_action(
                                {name, static_cast<std::size_t>(name_length)}, action);
                            *output = action.value;
                            return status;
                          })
      .code();
}

extern "C" gneiss_result gneiss_game_context_get_action_state(gneiss_game_context context,
                                                              gneiss_action action,
                                                              gneiss_action_state* output) {
  return with_application(context,
                          [&](auto& application) {
                            if (!output || output->struct_size < GNEISS_ACTION_STATE_VERSION_1_SIZE)
                              return gneiss::engine::result::invalid_argument;
                            gneiss::engine::action_state state;
                            const auto status = application.get_action_state({action}, state);
                            *output = GNEISS_ACTION_STATE_INIT;
                            output->pressed = static_cast<uint8_t>(state.pressed);
                            output->held = static_cast<uint8_t>(state.held);
                            output->released = static_cast<uint8_t>(state.released);
                            output->value = state.value;
                            return status;
                          })
      .code();
}

extern "C" gneiss_result gneiss_game_context_request_exit(gneiss_game_context context) {
  return with_application(context,
                          [](auto& application) {
                            application.request_exit();
                            return gneiss::engine::result::success;
                          })
      .code();
}

extern "C" gneiss_result gneiss_game_context_get_entity_transform(gneiss_game_context context,
                                                                  gneiss_entity_id entity,
                                                                  gneiss_transform* output) {
  if (!output)
    return GNEISS_ERROR_INVALID_ARGUMENT;
  return with_application(context,
                          [&](auto& application) {
                            gneiss::engine::transform value;
                            const auto status =
                                application.native_world()->entity_get_local_transform(
                                    gneiss::entity_id{entity}, value);
                            if (status == gneiss::engine::result::success) {
                              std::ranges::copy(value.translation, output->translation);
                              std::ranges::copy(value.rotation, output->rotation);
                              std::ranges::copy(value.scale, output->scale);
                            }
                            return status;
                          })
      .code();
}

extern "C" gneiss_result gneiss_game_context_set_entity_transform(gneiss_game_context context,
                                                                  gneiss_entity_id entity,
                                                                  const gneiss_transform* input) {
  if (!input)
    return GNEISS_ERROR_INVALID_ARGUMENT;
  return with_application(context,
                          [&](auto& application) {
                            gneiss::engine::transform value;
                            std::ranges::copy(input->translation, value.translation.begin());
                            std::ranges::copy(input->rotation, value.rotation.begin());
                            std::ranges::copy(input->scale, value.scale.begin());
                            return application.native_world()->entity_set_local_transform(
                                gneiss::entity_id{entity}, value);
                          })
      .code();
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
