// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/api/scene_query_conversion.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"

#include <gneiss/engine/scene.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {
using gneiss::runtime::find_application;
using gneiss::runtime::validate_application;
} // namespace

extern "C" gneiss_result gneiss_scene_instance_load(gneiss_application application, const char* uri,
                                                    uint64_t uri_length,
                                                    gneiss_scene_instance* out_instance) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_instance == nullptr || uri == nullptr || uri_length == 0U ||
        uri_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_instance = GNEISS_NULL_SCENE_INSTANCE;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      const auto uri_view = std::string_view(uri, static_cast<std::size_t>(uri_length));
      const auto result = state->scenes()->load(uri_view, out_instance);
      if (result != gneiss::engine::result::success) {
        auto message = std::string{"场景加载失败："};
        message.append(uri_view);
        state->report(application, gneiss::engine::severity::error, gneiss::engine::category::asset,
                      result, "scene.load", message);
      }
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_unload(gneiss_application application,
                                                      gneiss_scene_instance instance) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->unload(instance)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_find_node(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         const char* uuid, uint64_t uuid_length,
                                                         gneiss_scene_node_id* out_node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_node == nullptr || uuid == nullptr || uuid_length == 0U ||
        uuid_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      return gneiss::engine::write_identifier<gneiss::scene_node_id>(
          *out_node, [&](auto& native_output) {
            return state->scenes()->find_node(
                instance, std::string_view(uuid, static_cast<std::size_t>(uuid_length)),
                native_output);
          });
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_get_node_count(gneiss_application application,
                                                              gneiss_scene_instance instance,
                                                              uint64_t* out_count) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_count == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_count = 0U;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->get_node_count(instance, out_count)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_scene_instance_get_node_info(gneiss_application application, gneiss_scene_instance instance,
                                    uint64_t index, gneiss_scene_instance_node_info* out_info) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_info == nullptr ||
        out_info->struct_size < GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      using gneiss::engine::node_query_detail;
      auto detail = node_query_detail::identity;
      if (out_info->struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_3_SIZE) {
        detail = node_query_detail::components;
      } else if (out_info->struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_2_SIZE) {
        detail = node_query_detail::assets;
      }
      gneiss::engine::scene_node_view value;
      const auto result = state->scenes()->get_node_info(instance, index, value, detail);
      gneiss::engine::detail::write_scene_node(value, *out_info);
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}
