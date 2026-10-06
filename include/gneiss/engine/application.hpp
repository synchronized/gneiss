// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_HPP_
#define GNEISS_APPLICATION_HPP_

#include <gneiss/engine/application.h>
#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/world.hpp>

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace gneiss {

/** 只借用 Application，不延长寿命；日志可跨线程，其余操作限创建线程。 */
class application_ref {
public:
  application_ref() noexcept = default;
  explicit application_ref(gneiss_application handle) noexcept : handle_(handle) {}
  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_APPLICATION; }
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }
  [[nodiscard]] result run(std::uint64_t max_frame_count = 0) const noexcept {
    return from_native(gneiss_application_run(handle_, max_frame_count));
  }
  [[nodiscard]] result request_exit() const noexcept {
    return from_native(gneiss_application_request_exit(handle_));
  }
  [[nodiscard]] result get_window_size(std::uint32_t& out_width,
                                       std::uint32_t& out_height) const noexcept {
    return from_native(gneiss_application_get_window_size(handle_, &out_width, &out_height));
  }
  [[nodiscard]] result set_paused(bool is_paused) const noexcept {
    return from_native(gneiss_application_set_paused(handle_, is_paused ? UINT8_C(1) : UINT8_C(0)));
  }
  /** 取出当帧输入事件；队列为空返回 not_ready，仅限创建线程。 */
  [[nodiscard]] result poll_input(input_event& output) const noexcept {
    return gneiss::poll_input(handle_, output);
  }
  /** 输出键盘值快照；仅限创建线程。 */
  [[nodiscard]] result get_keyboard_state(keyboard_state& output) const noexcept {
    return gneiss::get_keyboard_state(handle_, output);
  }
  /** 输出指针值快照；仅限创建线程。 */
  [[nodiscard]] result get_pointer_state(pointer_state& output) const noexcept {
    return gneiss::get_pointer_state(handle_, output);
  }
  /** 同步加载动作映射；成功使旧动作 ID 失效，失败保留原映射。 */
  [[nodiscard]] result load_action_map(std::string_view uri) const noexcept {
    return gneiss::load_action_map(handle_, uri);
  }
  /** 返回非拥有动作 ID；失败保留 output，仅限创建线程。 */
  [[nodiscard]] result find_action(std::string_view name, action_id& output) const noexcept {
    return gneiss::find_action(handle_, name, output);
  }
  [[nodiscard]] result get_action_state(action_id id, action_state& output) const noexcept {
    return gneiss::get_action_state(handle_, id, output);
  }
  [[nodiscard]] result get_world(gneiss_world& out_world) const noexcept {
    return from_native(gneiss_application_get_world(handle_, &out_world));
  }
  /** 获取借用 World 视图，不转移所有权；场景切换或 Application 销毁可能使其失效。 */
  [[nodiscard]] result get_world(world_ref& output) const noexcept {
    gneiss_world value{};
    const auto status = from_native(gneiss_application_get_world(handle_, &value));
    if (status.ok()) {
      output = world_ref{value};
    }
    return status;
  }
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh& output) const noexcept {
    return mesh::create(handle_, desc, output);
  }
  [[nodiscard]] result create_material(const material_desc& desc, material& output) const noexcept {
    return material::create(handle_, desc, output);
  }
  [[nodiscard]] result create_texture(const texture_desc& desc, texture& output) const noexcept {
    return texture::create(handle_, desc, output);
  }
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh_id& out_mesh) const noexcept {
    gneiss_mesh handle = GNEISS_NULL_MESH;
    const auto native_result = gneiss_mesh_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_mesh = mesh_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_mesh(mesh_id mesh) const noexcept {
    return from_native(gneiss_mesh_destroy(handle_, mesh.get()));
  }
  [[nodiscard]] result create_material(const material_desc& desc,
                                       material_id& out_material) const noexcept {
    gneiss_material handle = GNEISS_NULL_MATERIAL;
    const auto native_result = gneiss_material_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_material = material_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_material(material_id material) const noexcept {
    return from_native(gneiss_material_destroy(handle_, material.get()));
  }
  [[nodiscard]] result create_texture(const texture_desc& desc,
                                      texture_id& out_texture) const noexcept {
    gneiss_texture handle = GNEISS_NULL_TEXTURE;
    const auto native_result = gneiss_texture_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_texture = texture_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_texture(texture_id texture) const noexcept {
    return from_native(gneiss_texture_destroy(handle_, texture.get()));
  }
  /** 仅在 update 中提交当帧 UI；数组在返回前复制，纹理 RID 仍借用所属 Application 的资源。 */
  [[nodiscard]] result submit_ui_draw_list(const ui_draw_list_desc& desc) const noexcept {
    return from_native(gneiss_application_submit_ui_draw_list(handle_, &desc));
  }
  /** 仅在 update 中提交当帧调试线段；复制数组，不取得调用方所有权。 */
  [[nodiscard]] result submit_debug_draw_list(const debug_draw_list_desc& desc) const noexcept {
    return from_native(gneiss_application_submit_debug_draw_list(handle_, &desc));
  }
  /** 消息字符串在返回前复制，可从工作线程提交；不得与本包装的移动/reset 并发。
   * 接收回调串行执行，不能重入日志；回调 userdata 必须存活至 Application 关闭完成。 */
  [[nodiscard]] result log(const log_message& message) const noexcept {
    const auto native = to_native(message);
    return from_native(gneiss_application_log(handle_, &native));
  }

protected:
  gneiss_application handle_ = GNEISS_NULL_APPLICATION;
};

/** 平台选择；未知值在 create 中被拒绝。 */
// NOLINTNEXTLINE(performance-enum-size): 保留完整协议值，输入校验不截断未知位。
enum class application_platform : std::uint32_t { callback = 0, granit = 1 };
/** 窗口标志只能同类组合；未定义位在 create 中被拒绝。 */
// NOLINTNEXTLINE(performance-enum-size): 保留完整协议值，输入校验不截断未知位。
enum class application_window_flags : std::uint32_t {
  none = 0,
  visible = 1,
  resizable = 2,
  high_dpi = 4,
};
[[nodiscard]] constexpr application_window_flags
operator|(application_window_flags left, application_window_flags right) noexcept {
  return static_cast<application_window_flags>(static_cast<std::uint32_t>(left) |
                                               static_cast<std::uint32_t>(right));
}
/** 单调时钟的帧值快照，可复制保存。 */
struct frame_time {
  std::uint64_t frame_index{}, delta_ns{}, elapsed_ns{};
  bool is_paused{};
};
// NOLINTNEXTLINE(performance-enum-size): 保留完整协议值，输入校验不截断未知位。
enum class diagnostic_severity : std::uint32_t { info = 1, warning = 2, error = 3 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整协议值，输入校验不截断未知位。
enum class diagnostic_category : std::uint32_t {
  application = 1,
  asset = 2,
  input = 3,
  backend = 4,
};
/** 字符串只在同步诊断回调内借用。 */
struct diagnostic {
  diagnostic_severity severity = diagnostic_severity::info;
  diagnostic_category category = diagnostic_category::application;
  result operation = result::success;
  std::string_view module, message;
};
/** 回调必须自行处理异常，不能越过 noexcept 边界。user_data 由调用方持有至关闭完成。
 * 日志在专用消费线程调用，其他回调在创建线程调用；视图不能逃逸回调。 */
struct application_callbacks {
  void* user_data{};
  result (*initialize)(void*) noexcept = nullptr;
  result (*poll_events)(void*, bool&) noexcept = nullptr;
  std::uint64_t (*now_ns)(void*) noexcept = nullptr;
  result (*update)(application_ref, const frame_time&, void*) noexcept = nullptr;
  void (*shutdown)(void*) noexcept = nullptr;
  void (*diagnostic)(application_ref, const gneiss::diagnostic&, void*) noexcept = nullptr;
  bool (*close_requested)(application_ref, void*) noexcept = nullptr;
  void (*log)(application_ref, const log_event&, void*) noexcept = nullptr;
};
/** 配置字符串只借用到 create 返回；回调表复制到稳定存储，user_data 不复制。 */
struct application_desc {
  application_callbacks callbacks;
  application_platform platform = application_platform::callback;
  std::string_view window_title;
  std::uint32_t window_width = 1280, window_height = 720;
  application_window_flags window_flags = application_window_flags::visible |
                                          application_window_flags::resizable |
                                          application_window_flags::high_dpi;
  std::string_view asset_root, environment_asset;
  float environment_intensity = 1.0F, environment_rotation_radians = 0.0F;
};

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

class released_application;
/** 独占拥有 Application；移动与释放同时转移回调适配存储。关闭失败时保留所有权。 */
class application final : public application_ref {
public:
  application() noexcept = default;
  ~application() noexcept { reset_or_terminate(); }
  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&& other) noexcept
      : application_ref(std::exchange(other.handle_, GNEISS_NULL_APPLICATION)),
        callbacks_(std::move(other.callbacks_)) {}
  application& operator=(application&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_APPLICATION);
      callbacks_ = std::move(other.callbacks_);
    }
    return *this;
  }
  /** 创建失败保留输出；配置字符串只在调用期间借用。 */
  [[nodiscard]] static result create(const application_desc& desc, application& output) noexcept {
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
    const auto closed = output.reset();
    if (closed.failed()) {
      return closed;
    }
    output = std::move(candidate);
    return status;
  }
  /** 显式 C ABI 创建入口；原生回调和 user_data 寿命完全由调用方管理。 */
  [[nodiscard]] static result create_native(const gneiss_application_desc& desc,
                                            application& output) noexcept {
    application candidate;
    const auto status = from_native(gneiss_application_create(&desc, &candidate.handle_));
    if (status.failed()) {
      return status;
    }
    const auto closed = output.reset();
    if (closed.failed()) {
      return closed;
    }
    output = std::move(candidate);
    return status;
  }
  [[nodiscard]] application_ref ref() const noexcept { return application_ref{handle_}; }
  /** 返回携带回调存储的所有权载体；不能隐式转换为裸句柄。 */
  [[nodiscard]] released_application release() noexcept;
  /** 收回释放载体的所有权；输出关闭失败时载体保持不变。 */
  [[nodiscard]] static result adopt(released_application&& released, application& output) noexcept;
  /** 关闭等待日志排空；非失效错误保留句柄与回调存储，供创建线程重试。 */
  result reset() noexcept {
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

private:
  void reset_or_terminate() noexcept {
    if (reset().failed()) {
      std::terminate();
    }
  }
  std::unique_ptr<application_callbacks> callbacks_;
};

/** C 互操作所有权载体。get() 仅借用；载体必须存活至 C 操作和关闭完成。
 * 析构会关闭仍持有的 Application；须在创建线程销毁，可通过 adopt 转回 application。 */
class released_application final {
public:
  released_application() noexcept = default;
  released_application(released_application&&) noexcept = default;
  released_application& operator=(released_application&&) noexcept = default;
  released_application(const released_application&) = delete;
  released_application& operator=(const released_application&) = delete;
  [[nodiscard]] gneiss_application get() const noexcept { return owner_.get(); }
  result reset() noexcept { return owner_.reset(); }

private:
  friend class application;
  explicit released_application(application&& owner) noexcept : owner_(std::move(owner)) {}
  application owner_;
};
inline released_application application::release() noexcept {
  return released_application{std::move(*this)};
}
inline result application::adopt(released_application&& released, application& output) noexcept {
  const auto status = output.reset();
  if (status.failed()) {
    return status;
  }
  output = std::move(released.owner_);
  return result::success;
}

} // namespace gneiss

#endif
