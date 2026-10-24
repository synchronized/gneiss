// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/result.hpp"
#include "runtime/application_registry.hpp"

#include "runtime/application.hpp"
#include "runtime/application_asset_reload_internal.hpp"
#include "runtime/application_log_internal.hpp"
#include "runtime/application_scene_load_internal.hpp"
#include "runtime/application_view_internal.hpp"
#include <new>
#include <string_view>

gneiss::engine::core::result
gneiss::runtime::set_view_camera(gneiss_application application,
                                 const engine::render::view_camera* camera) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->set_view_camera(camera) : valid;
}

gneiss::engine::core::result
gneiss::runtime::create_mesh_node_ready(gneiss_application application, gneiss_scene_instance scene,
                                        const engine::scene::mesh_renderer_node_creation& desc,
                                        const mesh_binding& prepared,
                                        gneiss_scene_node_id& output) noexcept {
  output = GNEISS_NULL_SCENE_NODE_ID;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success
             ? engine::core::write_identifier<scene_node_id>(
                   output,
                   [&](auto& native_output) {
                     return state->create_mesh_node_ready(scene, desc, prepared, native_output);
                   })
             : valid;
}

gneiss::engine::core::result
gneiss::runtime::collect_unused_edit_assets(gneiss_application application) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->collect_unused_edit_assets()
                                                        : valid;
}

gneiss::engine::core::result
gneiss::runtime::query_ready_asset(gneiss_application application,
                                   const engine::render::render_asset_reload& source,
                                   engine::render::render_asset_lease& output) noexcept {
  output = {};
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->query_ready_asset(source, output)
                                                        : valid;
}

gneiss::engine::core::result gneiss::runtime::capture_mesh_binding(gneiss_application application,
                                                                   gneiss_scene_instance scene,
                                                                   gneiss_scene_node_id node,
                                                                   mesh_binding& output) noexcept {
  output = {};
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success
             ? state->capture_mesh_binding(scene, scene_node_id{node}, output)
             : valid;
}

gneiss::engine::core::result
gneiss::runtime::replace_mesh_binding(gneiss_application application, gneiss_scene_instance scene,
                                      gneiss_scene_node_id node, const mesh_binding& expected,
                                      const mesh_binding& replacement) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success
             ? state->replace_mesh_binding(scene, scene_node_id{node}, expected, replacement)
             : valid;
}

gneiss::engine::core::result
gneiss::runtime::prepare_edit_assets(gneiss_application application,
                                     std::span<const engine::render::render_asset_reload> sources,
                                     std::uint64_t revision, std::uint64_t& request) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success
             ? state->prepare_edit_assets(sources, revision, request)
             : valid;
}

gneiss::engine::core::result
gneiss::runtime::capture_frame(gneiss_application application, std::uint32_t width,
                               std::uint32_t height, engine::render::frame_image& output) noexcept {
  output = {};
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success
             ? state->capture_frame(width, height, output)
             : valid;
}

gneiss::engine::core::result
gneiss::runtime::query_render_statistics(gneiss_application application,
                                         engine::render::render_queue_stats& output) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid == gneiss::engine::core::result::success) {
    output = state->render_statistics();
  }
  return valid;
}

gneiss::engine::core::result
gneiss::runtime::attach_task_executor(gneiss_application application,
                                      engine::core::tasks::task_executor& executor) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->attach_task_executor(executor)
                                                        : valid;
}

gneiss::engine::core::result gneiss::runtime::request_scene_load(gneiss_application application,
                                                                 std::string_view uri,
                                                                 std::uint64_t session,
                                                                 std::uint64_t revision,
                                                                 std::uint64_t& request) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  try {
    return state->request_scene(uri, session, revision, request);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
gneiss::runtime::query_scene_retirement(gneiss_application application,
                                        scene_retirement_statistics& output) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success)
    return valid;
  output = state->scene_retirement();
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result
gneiss::runtime::query_scene_load_progress(gneiss_application application,
                                           scene_load_progress& progress, bool& active) noexcept {
  active = false;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  active = state->scene_progress(progress);
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result gneiss::runtime::poll_scene_load(gneiss_application application,
                                                              scene_load_completion& completion,
                                                              bool& ready) noexcept {
  ready = false;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  try {
    return state->poll_scene(completion, ready);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result gneiss::runtime::cancel_scene_load(gneiss_application application,
                                                                std::uint64_t request) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  try {
    return state->cancel_scene(request);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
gneiss::runtime::activate_scene_load(gneiss_application application, std::uint64_t request,
                                     scene_load_completion& completion) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  try {
    return state->activate_scene(request, completion);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result gneiss::runtime::request_render_assets(
    gneiss_application application, std::span<const engine::render::render_asset_reload> assets,
    std::uint64_t session, std::uint64_t revision, std::uint64_t& request, bool reload) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success)
    return valid;
  if (!state->has_asset_loading_service() || state->scene_loading()) {
    return gneiss::engine::core::result::not_ready;
  }
  try {
    return state->request_assets(assets, session, revision, request, reload);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
gneiss::runtime::query_asset_load_progress(gneiss_application application,
                                           engine::render::asset_load_progress& progress,
                                           bool& active) noexcept {
  active = false;
  progress = {};
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success)
    return valid;
  active = state->asset_progress(progress);
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result
gneiss::runtime::cancel_render_assets(gneiss_application application) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success)
    return valid;
  return state->cancel_assets();
}

gneiss::engine::core::result
gneiss::runtime::request_textures(gneiss_application application, std::span<const std::string> uris,
                                  std::uint64_t session, std::uint64_t revision,
                                  std::uint64_t& request, bool reload) noexcept {
  request = 0U;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  if (!state->has_asset_loading_service() || state->scene_loading()) {
    return gneiss::engine::core::result::not_ready;
  }
  try {
    if (uris.size() > engine::render::texture_load_service::maximum_batch) {
      return gneiss::engine::core::result::invalid_argument;
    }
    std::vector<engine::render::render_asset_reload> sources;
    sources.reserve(uris.size());
    for (const auto& uri : uris) {
      sources.push_back({.uri = uri, .type = engine::render::render_asset_type::texture});
    }
    return state->request_assets(sources, session, revision, request, reload);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
gneiss::runtime::poll_textures(gneiss_application application,
                               engine::render::texture_load_completion& completion,
                               bool& ready) noexcept {
  ready = false;
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  if (!state->has_asset_loading_service()) {
    return gneiss::engine::core::result::not_ready;
  }
  try {
    return state->poll_assets(completion, ready);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
gneiss::runtime::cancel_textures(gneiss_application application) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  if (valid != gneiss::engine::core::result::success) {
    return valid;
  }
  if (!state->has_asset_loading_service()) {
    return gneiss::engine::core::result::success;
  }
  const auto cancelled = state->cancel_assets();
  return cancelled == gneiss::engine::core::result::not_ready
             ? gneiss::engine::core::result::success
             : cancelled;
}

gneiss::engine::core::result gneiss::runtime::reload_render_assets(
    gneiss_application application,
    std::span<const engine::render::render_asset_reload> assets) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->reload_render_assets(assets)
                                                        : valid;
}

gneiss::engine::core::result gneiss::runtime::reload_scene(gneiss_application application,
                                                           gneiss_scene_instance instance,
                                                           std::string_view uri) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->reload_scene(instance, uri)
                                                        : valid;
}

gneiss::engine::core::result gneiss::runtime::reload_prefab(gneiss_application application,
                                                            gneiss_scene_instance instance,
                                                            std::string_view uri) noexcept {
  auto state = find_application(application);
  const auto valid = validate_application(state);
  return valid == gneiss::engine::core::result::success ? state->reload_prefab(instance, uri)
                                                        : valid;
}

gneiss::engine::core::result
gneiss::runtime::submit_application_log(gneiss_application application,
                                        const engine::core::log::message_view& message,
                                        std::string_view source) noexcept {
  try {
    auto state = find_application(application);
    if (state == nullptr) {
      return engine::core::result::invalid_handle;
    }
    auto value = message;
    value.context = application;
    value.source = source;
    return state->submit_log(value);
  } catch (...) {
    return engine::core::result::internal;
  }
}
