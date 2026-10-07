// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_registry.hpp"

#include "engine/function/application/application_asset_reload_internal.hpp"
#include "engine/function/application/application_log_internal.hpp"
#include "engine/function/application/application_scene_load_internal.hpp"
#include "engine/function/application/application_state.hpp"
#include <new>
#include <string_view>

gneiss_result
gneiss::application_internal::capture_frame(gneiss_application application, std::uint32_t width,
                                            std::uint32_t height,
                                            render_internal::frame_image& output) noexcept {
  output = {};
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == GNEISS_SUCCESS ? state->capture_frame(width, height, output) : valid;
}

gneiss_result gneiss::application_internal::query_render_statistics(
    gneiss_application application, render_internal::render_queue_stats& output) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid == GNEISS_SUCCESS) {
    output = state->render_statistics();
  }
  return valid;
}

gneiss_result
gneiss::application_internal::attach_task_executor(gneiss_application application,
                                                   tasks::task_executor& executor) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == GNEISS_SUCCESS ? state->attach_task_executor(executor) : valid;
}

gneiss_result gneiss::application_internal::request_scene_load(gneiss_application application,
                                                               std::string_view uri,
                                                               std::uint64_t session,
                                                               std::uint64_t revision,
                                                               std::uint64_t& request) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  if (!state->scene_service() || !state->can_start_scene_load()) {
    return GNEISS_ERROR_NOT_READY;
  }
  try {
    return state->scene_service()->submit(uri, session, revision, request);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
gneiss::application_internal::query_scene_retirement(gneiss_application application,
                                                     scene_retirement_statistics& output) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS)
    return valid;
  output = state->scene_retirement();
  return GNEISS_SUCCESS;
}

gneiss_result gneiss::application_internal::query_scene_load_progress(
    gneiss_application application, scene_load_progress& progress, bool& active) noexcept {
  active = false;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  if (state->scene_service()) {
    active = state->scene_service()->progress(progress);
  }
  return GNEISS_SUCCESS;
}

gneiss_result gneiss::application_internal::poll_scene_load(gneiss_application application,
                                                            scene_load_completion& completion,
                                                            bool& ready) noexcept {
  ready = false;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  if (!state->scene_service()) {
    return GNEISS_ERROR_NOT_READY;
  }
  try {
    state->advance_scene_load();
    ready = state->scene_service()->take(completion);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result gneiss::application_internal::cancel_scene_load(gneiss_application application,
                                                              std::uint64_t request) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  try {
    return state->scene_service() && state->scene_service()->cancel(request)
               ? GNEISS_SUCCESS
               : GNEISS_ERROR_NOT_READY;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
gneiss::application_internal::activate_scene_load(gneiss_application application,
                                                  std::uint64_t request,
                                                  scene_load_completion& completion) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  try {
    return state->activate_scene(request, completion);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result gneiss::application_internal::request_render_assets(
    gneiss_application application, std::span<const render_internal::render_asset_reload> assets,
    std::uint64_t session, std::uint64_t revision, std::uint64_t& request, bool reload) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS)
    return valid;
  if (!state->texture_service() || state->scene_loading())
    return GNEISS_ERROR_NOT_READY;
  try {
    return state->texture_service()->submit_assets(assets, session, revision, request, reload);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result gneiss::application_internal::query_asset_load_progress(
    gneiss_application application, render_internal::asset_load_progress& progress,
    bool& active) noexcept {
  active = false;
  progress = {};
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS)
    return valid;
  if (state->texture_service())
    active = state->texture_service()->progress(progress);
  return GNEISS_SUCCESS;
}

gneiss_result
gneiss::application_internal::cancel_render_assets(gneiss_application application) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS)
    return valid;
  return state->texture_service() && state->texture_service()->cancel() ? GNEISS_SUCCESS
                                                                        : GNEISS_ERROR_NOT_READY;
}

gneiss_result gneiss::application_internal::request_textures(
    gneiss_application application, std::span<const std::string> uris, std::uint64_t session,
    std::uint64_t revision, std::uint64_t& request, bool reload) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  if (!state->texture_service() || state->scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  try {
    return state->texture_service()->submit(uris, session, revision, request, reload);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
gneiss::application_internal::poll_textures(gneiss_application application,
                                            render_internal::texture_load_completion& completion,
                                            bool& ready) noexcept {
  ready = false;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  if (!state->texture_service()) {
    return GNEISS_ERROR_NOT_READY;
  }
  try {
    state->texture_service()->advance();
    ready = state->texture_service()->take(completion);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
gneiss::application_internal::cancel_textures(gneiss_application application) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != GNEISS_SUCCESS) {
    return valid;
  }
  if (state->texture_service()) {
    state->texture_service()->cancel();
  }
  return GNEISS_SUCCESS;
}

gneiss_result gneiss::application_internal::reload_render_assets(
    gneiss_application application,
    std::span<const render_internal::render_asset_reload> assets) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == GNEISS_SUCCESS ? state->reload_render_assets(assets) : valid;
}

gneiss_result gneiss::application_internal::reload_scene(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         std::string_view uri) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == GNEISS_SUCCESS ? state->reload_scene(instance, uri) : valid;
}

gneiss_result gneiss::application_internal::reload_prefab(gneiss_application application,
                                                          gneiss_scene_instance instance,
                                                          std::string_view uri) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == GNEISS_SUCCESS ? state->reload_prefab(instance, uri) : valid;
}

gneiss_result
gneiss::application_internal::submit_application_log(gneiss_application application,
                                                     const log_internal::message_view& message,
                                                     std::string_view source) noexcept {
  try {
    auto state = find_application(application);
    if (state == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    auto value = message;
    value.context = application;
    value.source = source;
    return state->submit_log(value);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
