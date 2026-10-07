// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_APPLICATION_INL_
#define GNEISS_DETAIL_APPLICATION_INL_

#include <gneiss/engine/application.hpp>

#include <limits>
#include <new>

namespace gneiss {

namespace detail {
inline std::string_view application_text(const char* text, std::uint64_t length) noexcept {
  return length == 0 ? std::string_view{}
                     : std::string_view{text, static_cast<std::size_t>(length)};
}
inline void bind_application_callbacks(gneiss_application_desc& desc,
                                       application_callbacks* callbacks) noexcept {
  desc.user_data = callbacks;
  if (callbacks->initialize != nullptr) {
    desc.initialize = [](void* data) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      return cb.initialize(cb.user_data).native();
    };
  }
  if (callbacks->poll_events != nullptr) {
    desc.poll_events = [](void* data, std::uint8_t* output) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      bool close{};
      auto status = cb.poll_events(cb.user_data, close);
      *output = close ? 1 : 0;
      return status.native();
    };
  }
  if (callbacks->now_ns != nullptr) {
    desc.now_ns = [](void* data) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      return cb.now_ns(cb.user_data);
    };
  }
  if (callbacks->shutdown != nullptr) {
    desc.shutdown = [](void* data) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      cb.shutdown(cb.user_data);
    };
  }
  if (callbacks->update != nullptr) {
    desc.update = [](gneiss_application handle, const gneiss_frame_time* time,
                     void* data) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      const frame_time value{
          .frame_index = time->frame_index,
          .delta_ns = time->delta_ns,
          .elapsed_ns = time->elapsed_ns,
          .is_paused = time->is_paused != 0,
      };
      return cb.update(application_ref{handle}, value, cb.user_data).native();
    };
  }
  if (callbacks->close_requested != nullptr) {
    desc.close_requested = [](gneiss_application handle, void* data) noexcept -> std::uint8_t {
      auto& cb = *static_cast<application_callbacks*>(data);
      return cb.close_requested(application_ref{handle}, cb.user_data) ? 1 : 0;
    };
  }
  if (callbacks->diagnostic != nullptr) {
    desc.diagnostic = [](gneiss_application handle, const gneiss_diagnostic* value,
                         void* data) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      const diagnostic event{
          .severity = static_cast<diagnostic_severity>(value->severity),
          .category = static_cast<diagnostic_category>(value->category),
          .operation = from_native(value->result),
          .module = application_text(value->module, value->module_length),
          .message = application_text(value->message, value->message_length),
      };
      cb.diagnostic(application_ref{handle}, event, cb.user_data);
    };
  }
  if (callbacks->log != nullptr) {
    desc.log = [](gneiss_application handle, const gneiss_log_event* value, void* data) noexcept {
      auto& cb = *static_cast<application_callbacks*>(data);
      const log_event event{
          .severity = static_cast<log_severity>(value->severity),
          .sequence = value->sequence,
          .timestamp_ns = value->timestamp_ns,
          .thread_id = value->thread_id,
          .source = application_text(value->source, value->source_length),
          .category = application_text(value->category, value->category_length),
          .message = application_text(value->message, value->message_length),
          .operation = from_native(value->result),
      };
      cb.log(application_ref{handle}, event, cb.user_data);
    };
  }
}
} // namespace detail

inline result application_ref::run(std::uint64_t max_frame_count) const noexcept {
  return from_native(gneiss_application_run(handle_, max_frame_count));
}

inline result application_ref::request_exit() const noexcept {
  return from_native(gneiss_application_request_exit(handle_));
}

inline result application_ref::get_window_size(std::uint32_t& out_width,
                                               std::uint32_t& out_height) const noexcept {
  return from_native(gneiss_application_get_window_size(handle_, &out_width, &out_height));
}

inline result application_ref::set_paused(bool is_paused) const noexcept {
  return from_native(gneiss_application_set_paused(handle_, is_paused ? UINT8_C(1) : UINT8_C(0)));
}

inline result application_ref::poll_input(input_event& output) const noexcept {
  return gneiss::poll_input(handle_, output);
}

inline result application_ref::get_keyboard_state(keyboard_state& output) const noexcept {
  return gneiss::get_keyboard_state(handle_, output);
}

inline result application_ref::get_pointer_state(pointer_state& output) const noexcept {
  return gneiss::get_pointer_state(handle_, output);
}

inline result application_ref::load_action_map(std::string_view uri) const noexcept {
  return gneiss::load_action_map(handle_, uri);
}

inline result application_ref::find_action(std::string_view name,
                                           action_id& output) const noexcept {
  return gneiss::find_action(handle_, name, output);
}

inline result application_ref::get_action_state(action_id id, action_state& output) const noexcept {
  return gneiss::get_action_state(handle_, id, output);
}

inline result application_ref::get_world(gneiss_world& out_world) const noexcept {
  return from_native(gneiss_application_get_world(handle_, &out_world));
}

inline result application_ref::get_world(world_ref& output) const noexcept {
  gneiss_world value{};
  const auto status = from_native(gneiss_application_get_world(handle_, &value));
  if (status.ok()) {
    output = world_ref{value};
  }
  return status;
}

inline result application_ref::create_mesh(const mesh_desc& desc, mesh& output) const noexcept {
  return mesh::create(handle_, desc, output);
}

inline result application_ref::create_material(const material_desc& desc,
                                               material& output) const noexcept {
  return material::create(handle_, desc, output);
}

inline result application_ref::create_texture(const texture_desc& desc,
                                              texture& output) const noexcept {
  return texture::create(handle_, desc, output);
}

inline result application_ref::create_mesh(const mesh_desc& desc,
                                           mesh_id& out_mesh) const noexcept {
  mesh candidate;
  const auto status = mesh::create(handle_, desc, candidate);
  if (status.ok()) {
    out_mesh = candidate.release();
  }
  return status;
}

inline result application_ref::destroy_mesh(mesh_id mesh) const noexcept {
  return from_native(gneiss_mesh_destroy(handle_, mesh.get()));
}

inline result application_ref::create_material(const material_desc& desc,
                                               material_id& out_material) const noexcept {
  material candidate;
  const auto status = material::create(handle_, desc, candidate);
  if (status.ok()) {
    out_material = candidate.release();
  }
  return status;
}

inline result application_ref::destroy_material(material_id material) const noexcept {
  return from_native(gneiss_material_destroy(handle_, material.get()));
}

inline result application_ref::create_texture(const texture_desc& desc,
                                              texture_id& out_texture) const noexcept {
  texture candidate;
  const auto status = texture::create(handle_, desc, candidate);
  if (status.ok()) {
    out_texture = candidate.release();
  }
  return status;
}

inline result application_ref::destroy_texture(texture_id texture) const noexcept {
  return from_native(gneiss_texture_destroy(handle_, texture.get()));
}

inline result application_ref::submit_ui_draw_list(const ui_draw_list_desc& desc) const noexcept {
  return detail::with_render_desc(desc, [&](const auto& native) {
    return gneiss_application_submit_ui_draw_list(handle_, &native);
  });
}

inline result
application_ref::submit_debug_draw_list(const debug_draw_list_desc& desc) const noexcept {
  return detail::with_render_desc(desc, [&](const auto& native) {
    return gneiss_application_submit_debug_draw_list(handle_, &native);
  });
}

inline result application_ref::log(const log_message& message) const noexcept {
  const auto native = to_native(message);
  return from_native(gneiss_application_log(handle_, &native));
}

inline result application::create(const application_desc& desc, application& output) noexcept {
  const auto limit = std::numeric_limits<std::uint32_t>::max();
  if (desc.window_title.size() > limit || desc.asset_root.size() > limit ||
      desc.environment_asset.size() > limit || static_cast<std::uint32_t>(desc.platform) > 1U ||
      (static_cast<std::uint32_t>(desc.window_flags) & ~7U) != 0U) {
    return result::invalid_argument;
  }
  application candidate;
  try {
    candidate.callbacks_ = std::make_unique<application_callbacks>(desc.callbacks);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  }
  gneiss_application_desc native = GNEISS_APPLICATION_DESC_INIT;
  native.platform = static_cast<std::uint32_t>(desc.platform);
  native.window_title = desc.window_title.data();
  native.window_title_length = static_cast<std::uint32_t>(desc.window_title.size());
  native.window_width = desc.window_width;
  native.window_height = desc.window_height;
  native.window_flags = static_cast<std::uint32_t>(desc.window_flags);
  native.asset_root = desc.asset_root.data();
  native.asset_root_length = static_cast<std::uint32_t>(desc.asset_root.size());
  native.environment_asset = desc.environment_asset.data();
  native.environment_asset_length = static_cast<std::uint32_t>(desc.environment_asset.size());
  native.environment_intensity = desc.environment_intensity;
  native.environment_rotation_radians = desc.environment_rotation_radians;
  detail::bind_application_callbacks(native, candidate.callbacks_.get());
  const auto status = from_native(gneiss_application_create(&native, &candidate.handle_));
  if (status.failed()) {
    return status;
  }
  return output.replace_with(std::move(candidate));
}

inline result application::create_native(const gneiss_application_desc& desc,
                                         application& output) noexcept {
  application candidate;
  const auto status = from_native(gneiss_application_create(&desc, &candidate.handle_));
  if (status.failed()) {
    return status;
  }
  return output.replace_with(std::move(candidate));
}

inline result application::reset() noexcept {
  if (handle_ == GNEISS_NULL_APPLICATION) {
    callbacks_.reset();
    return result::success;
  }
  const auto status = from_native(gneiss_application_destroy(handle_));
  if (status.failed() && status != result::invalid_handle) {
    return status;
  }
  handle_ = GNEISS_NULL_APPLICATION;
  callbacks_.reset();
  return result::success;
}

inline void application::reset_or_terminate() noexcept {
  if (reset().failed()) {
    std::terminate();
  }
}

inline released_application application::release() noexcept {
  return released_application{std::move(*this)};
}
inline result application::adopt(released_application&& released, application& output) noexcept {
  return output.replace_with(std::move(released.owner_));
}

inline result application::replace_with(application&& candidate) noexcept {
  const auto status = reset();
  if (status.failed()) {
    return status;
  }
  *this = std::move(candidate);
  return result::success;
}

inline result released_application::reset() noexcept { return owner_.reset(); }

} // namespace gneiss

#endif
