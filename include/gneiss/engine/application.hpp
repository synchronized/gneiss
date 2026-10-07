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
#include <memory>
#include <utility>

namespace gneiss {

/** 只借用 Application，不延长寿命；日志可跨线程，其余操作限创建线程。 */
class application_ref {
public:
  application_ref() noexcept = default;
  explicit application_ref(gneiss_application handle) noexcept : handle_(handle) {}
  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_APPLICATION; }
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }
  [[nodiscard]] result run(std::uint64_t max_frame_count = 0) const noexcept;
  [[nodiscard]] result request_exit() const noexcept;
  [[nodiscard]] result get_window_size(std::uint32_t& out_width,
                                       std::uint32_t& out_height) const noexcept;
  [[nodiscard]] result set_paused(bool is_paused) const noexcept;
  /** 取出当帧输入事件；队列为空返回 not_ready，仅限创建线程。 */
  [[nodiscard]] result poll_input(input_event& output) const noexcept;
  /** 输出键盘值快照；仅限创建线程。 */
  [[nodiscard]] result get_keyboard_state(keyboard_state& output) const noexcept;
  /** 输出指针值快照；仅限创建线程。 */
  [[nodiscard]] result get_pointer_state(pointer_state& output) const noexcept;
  /** 同步加载动作映射；成功使旧动作 ID 失效，失败保留原映射。 */
  [[nodiscard]] result load_action_map(std::string_view uri) const noexcept;
  /** 返回非拥有动作 ID；失败保留 output，仅限创建线程。 */
  [[nodiscard]] result find_action(std::string_view name, action_id& output) const noexcept;
  [[nodiscard]] result get_action_state(action_id id, action_state& output) const noexcept;
  [[nodiscard]] result get_world(gneiss_world& out_world) const noexcept;
  /** 获取借用 World 视图，不转移所有权；场景切换或 Application 销毁可能使其失效。 */
  [[nodiscard]] result get_world(world_ref& output) const noexcept;
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh& output) const noexcept;
  [[nodiscard]] result create_material(const material_desc& desc, material& output) const noexcept;
  [[nodiscard]] result create_texture(const texture_desc& desc, texture& output) const noexcept;
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh_id& out_mesh) const noexcept;
  [[nodiscard]] result destroy_mesh(mesh_id mesh) const noexcept;
  [[nodiscard]] result create_material(const material_desc& desc,
                                       material_id& out_material) const noexcept;
  [[nodiscard]] result destroy_material(material_id material) const noexcept;
  [[nodiscard]] result create_texture(const texture_desc& desc,
                                      texture_id& out_texture) const noexcept;
  [[nodiscard]] result destroy_texture(texture_id texture) const noexcept;
  /** 仅在 update 中提交当帧 UI；数组在返回前复制，纹理 RID 仍借用所属 Application 的资源。 */
  [[nodiscard]] result submit_ui_draw_list(const ui_draw_list_desc& desc) const noexcept;
  /** 仅在 update 中提交当帧调试线段；复制数组，不取得调用方所有权。 */
  [[nodiscard]] result submit_debug_draw_list(const debug_draw_list_desc& desc) const noexcept;
  /** 消息字符串在返回前复制，可从工作线程提交；不得与本包装的移动/reset 并发。
   * 接收回调串行执行，不能重入日志；回调 userdata 必须存活至 Application 关闭完成。 */
  [[nodiscard]] result log(const log_message& message) const noexcept;

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
// 显式默认值允许指定初始化器省略非标量字段，避免缺字段诊断。
// NOLINTBEGIN(readability-redundant-member-init)
struct application_desc {
  application_callbacks callbacks{};
  application_platform platform = application_platform::callback;
  std::string_view window_title{};
  std::uint32_t window_width = 1280, window_height = 720;
  application_window_flags window_flags = application_window_flags::visible |
                                          application_window_flags::resizable |
                                          application_window_flags::high_dpi;
  std::string_view asset_root{}, environment_asset{};
  float environment_intensity = 1.0F, environment_rotation_radians = 0.0F;
};

// NOLINTEND(readability-redundant-member-init)

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
  [[nodiscard]] static result create(const application_desc& desc, application& output) noexcept;
  /** 显式 C ABI 创建入口；原生回调和 user_data 寿命完全由调用方管理。 */
  [[nodiscard]] static result create_native(const gneiss_application_desc& desc,
                                            application& output) noexcept;
  [[nodiscard]] application_ref ref() const noexcept { return application_ref{handle_}; }
  /** 返回携带回调存储的所有权载体；不能隐式转换为裸句柄。 */
  [[nodiscard]] released_application release() noexcept;
  /** 收回释放载体的所有权；输出关闭失败时载体保持不变。 */
  [[nodiscard]] static result adopt(released_application&& released, application& output) noexcept;
  /** 关闭等待日志排空；非失效错误保留句柄与回调存储，供创建线程重试。 */
  result reset() noexcept;

private:
  result replace_with(application&& candidate) noexcept;
  void reset_or_terminate() noexcept;
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
  result reset() noexcept;

private:
  friend class application;
  explicit released_application(application&& owner) noexcept : owner_(std::move(owner)) {}
  application owner_;
};

} // namespace gneiss

// 实现随 SDK 安装；使用者只需包含本模块头。
#include <gneiss/engine/detail/application.inl>

#endif
