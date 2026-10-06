// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/log_validation.hpp"
#include "engine/function/application/application_log_internal.hpp"
#include "engine/function/application/application_registry.hpp"
#include "engine/function/application/application_state.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/asset.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {

using gneiss::application_internal::find_application;
using gneiss::application_internal::validate_application;

void report_create_failure(const gneiss_application_desc& desc, gneiss_result result,
                           std::string_view module, std::string_view message) noexcept {
  if (desc.diagnostic == nullptr) {
    return;
  }
  const gneiss_diagnostic diagnostic = {
      .struct_size = sizeof(gneiss_diagnostic),
      .severity = GNEISS_DIAGNOSTIC_ERROR,
      .category = GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION,
      .result = result,
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
  if (out_application == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_application = GNEISS_NULL_APPLICATION;
  if (desc == nullptr || desc->struct_size < GNEISS_APPLICATION_DESC_VERSION_1_SIZE ||
      desc->reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  gneiss_application_desc normalized_desc = GNEISS_APPLICATION_DESC_INIT;
  std::memcpy(&normalized_desc, desc,
              std::min<std::size_t>(desc->struct_size, sizeof(gneiss_application_desc)));
  if (normalized_desc.platform > GNEISS_APPLICATION_PLATFORM_GRANIT ||
      normalized_desc.asset_reserved != 0U ||
      ((normalized_desc.environment_asset == nullptr) !=
       (normalized_desc.environment_asset_length == 0U)) ||
      (normalized_desc.environment_asset != nullptr &&
       gneiss_asset_uri_validate(normalized_desc.environment_asset,
                                 normalized_desc.environment_asset_length) != GNEISS_SUCCESS) ||
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
    report_create_failure(normalized_desc, GNEISS_ERROR_INVALID_ARGUMENT,
                          "application.configuration", "Application 创建参数无效");
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  // 保留资产根目录空指针/长度不匹配时的结果；有效的空配置表示不挂载目录。
  if ((normalized_desc.asset_root == nullptr) != (normalized_desc.asset_root_length == 0U)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const gneiss::application_internal::application_configuration config{
      .callbacks =
          {
              .user_data = normalized_desc.user_data,
              .initialize = normalized_desc.initialize,
              .poll_events = normalized_desc.poll_events,
              .now_ns = normalized_desc.now_ns,
              .update = normalized_desc.update,
              .shutdown = normalized_desc.shutdown,
              .diagnostic = normalized_desc.diagnostic,
              .close_requested = normalized_desc.close_requested,
              .log = normalized_desc.log,
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
              .resizable =
                  (normalized_desc.window_flags & GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT) != 0U,
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
  return gneiss::application_internal::create_application(config, *out_application);
}

extern "C" gneiss_result gneiss_application_destroy(gneiss_application application) {
  return gneiss::application_internal::destroy_application(application);
}

extern "C" gneiss_result gneiss_application_run(gneiss_application application,
                                                uint64_t max_frame_count) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const auto result = state->run(application, max_frame_count);
    if (result != GNEISS_SUCCESS) {
      state->report(application, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
                    result, "application", "主循环因运行时错误终止");
    }
    return result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_request_exit(gneiss_application application) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    state->request_exit();
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_get_window_size(gneiss_application application,
                                                            uint32_t* out_width,
                                                            uint32_t* out_height) {
  if (out_width == nullptr || out_height == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_width = 0U;
  *out_height = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->get_window_size(*out_width, *out_height)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数具有不同语义和取值范围。
extern "C" gneiss_result gneiss_application_set_paused(gneiss_application application,
                                                       uint8_t is_paused) {
  if (is_paused > UINT8_C(1)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    state->set_paused(is_paused != 0U);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_log(gneiss_application application,
                                                const gneiss_log_message* message) {
  const auto message_result = gneiss::abi_internal::validate_log_message(message);
  if (message_result != GNEISS_SUCCESS) {
    return message_result;
  }
  return gneiss::application_internal::submit_application_log(
      application, gneiss::abi_internal::log_message_view(*message), "application");
}

extern "C" gneiss_result gneiss_application_get_world(gneiss_application application,
                                                      gneiss_world* out_world) {
  if (out_world == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_world = GNEISS_NULL_WORLD;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    *out_world = state->world();
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
