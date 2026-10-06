// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GAME_MODULE_HPP_
#define GNEISS_GAME_MODULE_HPP_

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/game_module.h>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>
#include <gneiss/engine/world.hpp>

#include <cstdint>
#include <string_view>

namespace gneiss {

inline constexpr std::uint32_t game_module_abi_version_1 = GNEISS_GAME_MODULE_ABI_VERSION_1;
inline constexpr std::uint32_t game_module_abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT;
inline constexpr std::string_view game_module_query_symbol = GNEISS_GAME_MODULE_QUERY_SYMBOL;

/** 不拥有 Engine Game Context 的强类型句柄。 */
class game_context final {
public:
  constexpr game_context() noexcept = default;
  explicit constexpr game_context(gneiss_game_context value) noexcept : value_(value) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return value_ != GNEISS_NULL_GAME_CONTEXT;
  }
  [[nodiscard]] constexpr gneiss_game_context get() const noexcept { return value_; }

  [[nodiscard]] result get_world_native(gneiss_world& out_world) const noexcept {
    return from_native(gneiss_game_context_get_world(value_, &out_world));
  }
  /** 借用当前 World，不取得所有权；上下文失效或场景切换后须重新获取。失败保留输出。 */
  [[nodiscard]] result get_world(world_ref& output) const noexcept {
    gneiss_world value = GNEISS_NULL_WORLD;
    const auto status = get_world_native(value);
    if (status.ok()) {
      output = world_ref{value};
    }
    return status;
  }

  [[nodiscard]] result get_startup_root_entity_native(gneiss_entity_id& out_entity) const noexcept {
    return from_native(gneiss_game_context_get_startup_root_entity(value_, &out_entity));
  }
  /** 借用启动根实体；仅限模块生命周期回调线程，失败保留输出。 */
  [[nodiscard]] result get_startup_root_entity(entity_id& output) const noexcept {
    gneiss_entity_id value = GNEISS_NULL_ENTITY_ID;
    const auto status = get_startup_root_entity_native(value);
    if (status.ok()) {
      output = entity_id{value};
    }
    return status;
  }

  [[nodiscard]] result find_action_native(std::string_view name,
                                          gneiss_action& out_action) const noexcept {
    return from_native(
        gneiss_game_context_find_action(value_, name.data(), name.size(), &out_action));
  }

  [[nodiscard]] result get_action_state_native(gneiss_action action,
                                               gneiss_action_state& out_state) const noexcept {
    return from_native(gneiss_game_context_get_action_state(value_, action, &out_state));
  }
  /** 返回借用动作 ID；失败保留输出，仅限模块生命周期回调线程。 */
  [[nodiscard]] result find_action(std::string_view name, action_id& output) const noexcept {
    gneiss_action value = GNEISS_NULL_ACTION;
    const auto status = find_action_native(name, value);
    if (status.ok()) {
      output = action_id{value};
    }
    return status;
  }
  [[nodiscard]] result get_action_state(action_id id, action_state& output) const noexcept {
    gneiss_action_state native = GNEISS_ACTION_STATE_INIT;
    const auto status = get_action_state_native(id.get(), native);
    if (status.ok()) {
      output = detail::from_action_state(native);
    }
    return status;
  }

  [[nodiscard]] result request_exit() const noexcept {
    return from_native(gneiss_game_context_request_exit(value_));
  }

  [[nodiscard]] result log(const log_message& message) const noexcept {
    const auto native = to_native(message);
    return from_native(gneiss_game_context_log(value_, &native));
  }

  friend constexpr bool operator==(game_context, game_context) noexcept = default;

private:
  gneiss_game_context value_ = GNEISS_NULL_GAME_CONTEXT;
};

inline constexpr game_context null_game_context{};

/** 显式校验 C ABI 描述，不取得所有权。 */
[[nodiscard]] inline result
validate_game_module_native(const gneiss_game_module_desc& desc) noexcept {
  return from_native(gneiss_game_module_validate(&desc));
}

/** 单次逻辑更新时间；仅在当前回调内借用，单位为纳秒。 */
struct game_update_time final {
  std::uint64_t update_index = 0;
  std::uint64_t delta_ns = 0;
  std::uint64_t elapsed_ns = 0;
};

/** 模块名称借用至动态库卸载；不包含 ABI 布局字段。 */
struct game_module_desc final {
  // 显式默认值保证仅指定 ABI 版本时也不触发缺字段诊断。
  std::string_view module_id{}; // NOLINT(readability-redundant-member-init)
  std::uint32_t abi_version = game_module_abi_version;
};

/**
 * 原生模块回调，均在 Runtime 主线程执行且不得抛异常。
 * initialize 成功时交出私有状态，由 shutdown 销毁；失败时须自行回收候选状态。
 * shutdown 在初始化成功后最多调用一次。上下文与时间参数不取得所有权。
 */
struct game_module_callbacks final {
  result (*initialize)(game_context, void*&) noexcept = nullptr;
  result (*fixed_update)(game_context, void*, const game_update_time&) noexcept = nullptr;
  result (*update)(game_context, void*, const game_update_time&) noexcept = nullptr;
  result (*shutdown)(game_context, void*) noexcept = nullptr;
};

/**
 * 编译期生成模块 C ABI 桥接，不保存第二份运行时状态。
 * 四个回调必须齐全；真正的动态库导出入口仍使用 C ABI。
 */
template <game_module_callbacks Callbacks> class game_module final {
public:
  /** 显式构造 C 描述；仅借用 module_id，不进行校验。 */
  [[nodiscard]] static gneiss_game_module_desc to_native(game_module_desc desc) noexcept {
    gneiss_game_module_desc native{
        .struct_size = sizeof(gneiss_game_module_desc),
        .abi_version = desc.abi_version,
        .module_id = desc.module_id.data(),
        .module_id_length = desc.module_id.size(),
        .initialize = nullptr,
        .fixed_update = nullptr,
        .update = nullptr,
        .shutdown = nullptr,
        .reserved = {},
    };
    // 不实例化缺失回调的桥接，避免编译器生成空函数指针调用。
    if constexpr (Callbacks.initialize != nullptr) {
      native.initialize = initialize;
    }
    if constexpr (Callbacks.fixed_update != nullptr) {
      native.fixed_update = fixed_update;
    }
    if constexpr (Callbacks.update != nullptr) {
      native.update = update;
    }
    if constexpr (Callbacks.shutdown != nullptr) {
      native.shutdown = shutdown;
    }
    return native;
  }

  [[nodiscard]] static result validate(game_module_desc desc) noexcept {
    return validate_game_module_native(to_native(desc));
  }

  /**
   * 用于固定 C 导出入口；失败保留输出。只写当前已知字段，不触碰调用方的扩展尾部。
   * 调用方必须提供实际可写的描述空间并填写 struct_size；同步调用，可并发使用。
   */
  [[nodiscard]] static result export_query(std::uint32_t engine_abi_version,
                                           gneiss_game_module_desc* output,
                                           game_module_desc desc) noexcept {
    if (output == nullptr) {
      return result::invalid_argument;
    }
    if (engine_abi_version != game_module_abi_version ||
        output->struct_size < sizeof(gneiss_game_module_desc)) {
      return result::unsupported;
    }
    auto native = to_native(desc);
    const auto status = validate_game_module_native(native);
    if (status.ok()) {
      native.struct_size = output->struct_size;
      *output = native;
    }
    return status;
  }

private:
  static gneiss_result initialize(gneiss_game_context context, void** output) noexcept {
    if (output == nullptr) {
      return result::invalid_argument.native();
    }
    void* state = nullptr;
    const auto status = Callbacks.initialize(game_context{context}, state);
    if (status.ok()) {
      *output = state;
    }
    return status.native();
  }

  template <auto Callback>
  static gneiss_result invoke_update(gneiss_game_context context, void* state,
                                     const gneiss_game_update_time* time) noexcept {
    if (time == nullptr || time->struct_size < GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE ||
        time->reserved != 0) {
      return result::invalid_argument.native();
    }
    const game_update_time value{
        .update_index = time->update_index,
        .delta_ns = time->delta_ns,
        .elapsed_ns = time->elapsed_ns,
    };
    return Callback(game_context{context}, state, value).native();
  }

  static gneiss_result fixed_update(gneiss_game_context context, void* state,
                                    const gneiss_game_update_time* time) noexcept {
    return invoke_update<Callbacks.fixed_update>(context, state, time);
  }
  static gneiss_result update(gneiss_game_context context, void* state,
                              const gneiss_game_update_time* time) noexcept {
    return invoke_update<Callbacks.update>(context, state, time);
  }
  static gneiss_result shutdown(gneiss_game_context context, void* state) noexcept {
    return Callbacks.shutdown(game_context{context}, state).native();
  }
};

} // namespace gneiss

#endif
