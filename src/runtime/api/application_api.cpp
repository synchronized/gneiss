// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/log_validation.hpp"
#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include "runtime/api/application_log_sink.hpp"
#include "runtime/application.hpp"
#include "runtime/application_log_internal.hpp"
#include "runtime/application_registry.hpp"

#include "engine/resource/vfs/asset_uri.hpp"
#include <gneiss/engine/application.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {

using gneiss::runtime::find_application;
using gneiss::runtime::validate_application;

void report_create_failure(const gneiss_application_desc& desc, gneiss::engine::result result,
                           std::string_view module, std::string_view message) noexcept {
  if (desc.diagnostic == nullptr) {
    return;
  }
  const gneiss_diagnostic diagnostic = {
      .struct_size = sizeof(gneiss_diagnostic),
      .severity = GNEISS_DIAGNOSTIC_ERROR,
      .category = GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION,
      .result = gneiss::engine::detail::to_c_result(result),
      .module = module.data(),
      .module_length = module.size(),
      .message = message.data(),
      .message_length = message.size(),
      .reserved = {},
  };
  desc.diagnostic(GNEISS_NULL_APPLICATION, &diagnostic, desc.user_data);
}

} // namespace

extern "C" gneiss_result gneiss_application_create(const gneiss_application_desc* desc,
                                                   gneiss_application* out_application) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_application == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_application = GNEISS_NULL_APPLICATION;
    if (desc == nullptr || desc->struct_size < GNEISS_APPLICATION_DESC_VERSION_1_SIZE ||
        desc->reserved != 0U) {
      return gneiss::engine::result::invalid_argument;
    }
    gneiss_application_desc normalized_desc = GNEISS_APPLICATION_DESC_INIT;
    std::memcpy(&normalized_desc, desc,
                std::min<std::size_t>(desc->struct_size, sizeof(gneiss_application_desc)));
    if (normalized_desc.platform > GNEISS_APPLICATION_PLATFORM_GRANIT ||
        normalized_desc.asset_reserved != 0U ||
        ((normalized_desc.environment_asset == nullptr) !=
         (normalized_desc.environment_asset_length == 0U)) ||
        (normalized_desc.environment_asset != nullptr &&
         gneiss::engine::validate_uri(
             {normalized_desc.environment_asset, normalized_desc.environment_asset_length}) !=
             gneiss::engine::result::success) ||
        !std::isfinite(normalized_desc.environment_intensity) ||
        normalized_desc.environment_intensity < 0.0F ||
        !std::isfinite(normalized_desc.environment_rotation_radians) ||
        normalized_desc.environment_reserved != 0U ||
        (normalized_desc.window_title == nullptr && normalized_desc.window_title_length != 0U) ||
        (normalized_desc.window_flags &
         ~(GNEISS_APPLICATION_WINDOW_VISIBLE_BIT | GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT |
           GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT)) != 0U ||
        (normalized_desc.platform == GNEISS_APPLICATION_PLATFORM_GRANIT &&
         (normalized_desc.window_width == 0U || normalized_desc.window_height == 0U ||
          normalized_desc.initialize != nullptr || normalized_desc.poll_events != nullptr ||
          normalized_desc.shutdown != nullptr))) {
      report_create_failure(normalized_desc, gneiss::engine::result::invalid_argument,
                            "application.configuration", "Application 创建参数无效");
      return gneiss::engine::result::invalid_argument;
    }

    // 保留资产根目录空指针/长度不匹配时的结果；有效的空配置表示不挂载目录。
    if ((normalized_desc.asset_root == nullptr) != (normalized_desc.asset_root_length == 0U)) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      gneiss::runtime::application_configuration config{
          .callbacks =
              {
                  .initialize = {},
                  .poll_events = {},
                  .now_ns = {},
                  .update = {},
                  .shutdown = {},
                  .diagnostic = {},
                  .close_requested = {},
                  .log = normalized_desc.log == nullptr
                             ? gneiss::engine::event_sink{}
                             : gneiss::runtime::detail::make_application_log_sink(
                                   normalized_desc.log, normalized_desc.user_data),
              },
          .use_granit_window = normalized_desc.platform == GNEISS_APPLICATION_PLATFORM_GRANIT,
          .window =
              {
                  .title = normalized_desc.window_title_length == 0U
                               ? std::string_view{"Gneiss"}
                               : std::string_view{normalized_desc.window_title,
                                                  normalized_desc.window_title_length},
                  .width = normalized_desc.window_width,
                  .height = normalized_desc.window_height,
                  .visible =
                      (normalized_desc.window_flags & GNEISS_APPLICATION_WINDOW_VISIBLE_BIT) != 0U,
                  .resizable = (normalized_desc.window_flags &
                                GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT) != 0U,
                  .high_dpi =
                      (normalized_desc.window_flags & GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT) != 0U,
              },
          .asset_root =
              normalized_desc.asset_root == nullptr
                  ? std::string_view{}
                  : std::string_view{normalized_desc.asset_root, normalized_desc.asset_root_length},
          .environment_asset = normalized_desc.environment_asset == nullptr
                                   ? std::string_view{}
                                   : std::string_view{normalized_desc.environment_asset,
                                                      normalized_desc.environment_asset_length},
          .environment_intensity = normalized_desc.environment_intensity,
          .environment_rotation_radians = normalized_desc.environment_rotation_radians,
      };
      // C 回调只在 ABI 边界适配；不捕获描述结构或调用方输出地址。
      auto* const user_data = normalized_desc.user_data;
      if (const auto callback = normalized_desc.now_ns) {
        config.callbacks.now_ns = [callback, user_data] { return callback(user_data); };
      }
      if (const auto callback = normalized_desc.update) {
        config.callbacks.update = [callback, user_data](gneiss::engine::rid context,
                                                        const gneiss::engine::frame_time& time) {
          const gneiss_frame_time abi_time{time.frame_index,
                                           time.delta_ns,
                                           time.elapsed_ns,
                                           static_cast<std::uint8_t>(time.is_paused),
                                           {}};
          return gneiss::engine::detail::from_c_result(callback(context, &abi_time, user_data));
        };
      }
      if (const auto callback = normalized_desc.close_requested) {
        config.callbacks.close_requested = [callback, user_data](gneiss::engine::rid context) {
          return callback(context, user_data) != 0U;
        };
      }
      if (const auto callback = normalized_desc.diagnostic) {
        config.callbacks.diagnostic = [callback,
                                       user_data](gneiss::engine::rid context,
                                                  const gneiss::engine::diagnostic_view& event) {
          const gneiss_diagnostic abi_event{sizeof(gneiss_diagnostic),
                                            static_cast<std::uint32_t>(event.level),
                                            static_cast<std::uint32_t>(event.source),
                                            gneiss::engine::detail::to_c_result(event.result),
                                            event.module.data(),
                                            event.module.size(),
                                            event.message.data(),
                                            event.message.size(),
                                            {}};
          callback(context, &abi_event, user_data);
        };
      }
      if (const auto callback = normalized_desc.initialize) {
        config.callbacks.initialize = [callback, user_data] {
          return gneiss::engine::detail::from_c_result(callback(user_data));
        };
      }
      if (const auto callback = normalized_desc.poll_events) {
        config.callbacks.poll_events = [callback, user_data](bool& should_close) {
          std::uint8_t close{};
          const auto operation = callback(user_data, &close);
          should_close = close != 0U;
          return gneiss::engine::detail::from_c_result(operation);
        };
      }
      if (const auto callback = normalized_desc.shutdown) {
        config.callbacks.shutdown = [callback, user_data] { callback(user_data); };
      }
      return gneiss::runtime::create_application(config, *out_application);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_destroy(gneiss_application application) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    return gneiss::runtime::destroy_application(application);
  }());
}

extern "C" gneiss_result gneiss_application_run(gneiss_application application,
                                                uint64_t max_frame_count) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      const auto result = state->run(application, max_frame_count);
      if (result != gneiss::engine::result::success) {
        state->report(application, gneiss::engine::severity::error,
                      gneiss::engine::category::backend, result, "application",
                      "主循环因运行时错误终止");
      }
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_request_exit(gneiss_application application) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      state->request_exit();
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_get_window_size(gneiss_application application,
                                                            uint32_t* out_width,
                                                            uint32_t* out_height) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_width == nullptr || out_height == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_width = 0U;
    *out_height = 0U;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->get_window_size(*out_width, *out_height)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数具有不同语义和取值范围。
extern "C" gneiss_result gneiss_application_set_paused(gneiss_application application,
                                                       uint8_t is_paused) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (is_paused > UINT8_C(1)) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      state->set_paused(is_paused != 0U);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_log(gneiss_application application,
                                                const gneiss_log_message* message) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    const auto message_result = gneiss::engine::detail::validate_log_message(message);
    if (message_result != gneiss::engine::result::success) {
      return message_result;
    }
    return gneiss::runtime::submit_application_log(
        application, gneiss::engine::detail::log_message_view(*message), "application");
  }());
}

extern "C" gneiss_result gneiss_application_get_world(gneiss_application application,
                                                      gneiss_world* out_world) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_world == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_world = GNEISS_NULL_WORLD;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      *out_world = state->world();
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}
