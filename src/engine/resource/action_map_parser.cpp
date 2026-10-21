// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/action_map.hpp"

#include <yyjson.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <unordered_set>

namespace {

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

[[nodiscard]] std::string_view string_value(yyjson_val* value) {
  return {yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] bool has_exact_fields(yyjson_val* object, std::initializer_list<const char*> fields) {
  if (!yyjson_is_obj(object) || yyjson_obj_size(object) != fields.size()) {
    return false;
  }
  for (const auto* field : fields) {
    if (yyjson_obj_get(object, field) == nullptr) {
      return false;
    }
  }
  return true;
}

} // namespace

namespace gneiss::asset_internal {

result parse_action_map(std::string_view json, action_map& out_map) noexcept {
  try {
    yyjson_read_err error{};
    document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                           YYJSON_READ_NOFLAG, nullptr, &error),
                          &yyjson_doc_free);
    if (document == nullptr) {
      return result::invalid_argument;
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    if (!has_exact_fields(root, {"format", "version", "actions"})) {
      return result::invalid_argument;
    }
    yyjson_val* format = yyjson_obj_get(root, "format");
    yyjson_val* version = yyjson_obj_get(root, "version");
    yyjson_val* actions = yyjson_obj_get(root, "actions");
    if (!yyjson_is_str(format) || string_value(format) != "gneiss.input-map" ||
        !yyjson_is_uint(version) || yyjson_get_uint(version) != 1U || !yyjson_is_arr(actions)) {
      return result::invalid_argument;
    }

    action_map candidate;
    std::unordered_set<std::string> names;
    std::size_t action_index = 0;
    std::size_t action_count = 0;
    yyjson_val* action_value = nullptr;
    yyjson_arr_foreach(actions, action_index, action_count, action_value) {
      if (!has_exact_fields(action_value, {"name", "bindings"})) {
        return result::invalid_argument;
      }
      yyjson_val* name = yyjson_obj_get(action_value, "name");
      yyjson_val* bindings = yyjson_obj_get(action_value, "bindings");
      if (!yyjson_is_str(name) || yyjson_get_len(name) == 0U || !yyjson_is_arr(bindings) ||
          yyjson_arr_size(bindings) == 0U) {
        return result::invalid_argument;
      }
      action_definition action{.name = std::string(string_value(name)), .bindings = {}};
      if (!names.insert(action.name).second) {
        return result::invalid_argument;
      }
      std::size_t binding_index = 0;
      std::size_t binding_count = 0;
      yyjson_val* binding_value = nullptr;
      yyjson_arr_foreach(bindings, binding_index, binding_count, binding_value) {
        if (!has_exact_fields(binding_value, {"key", "scale"})) {
          return result::invalid_argument;
        }
        yyjson_val* key = yyjson_obj_get(binding_value, "key");
        yyjson_val* scale = yyjson_obj_get(binding_value, "scale");
        const auto key_value = yyjson_get_uint(key);
        const auto scale_value = yyjson_get_real(scale);
        if (!yyjson_is_uint(key) || key_value == 0U || key_value >= 256U || !yyjson_is_num(scale) ||
            !std::isfinite(scale_value) || scale_value < -1.0 || scale_value > 1.0 ||
            scale_value == 0.0) {
          return result::invalid_argument;
        }
        action.bindings.push_back({.physical_key = static_cast<std::uint32_t>(key_value),
                                   .scale = static_cast<float>(scale_value)});
      }
      candidate.actions.push_back(std::move(action));
    }
    if (candidate.actions.empty()) {
      return result::invalid_argument;
    }
    if (const auto valid = validate_action_map(candidate); valid.failed()) {
      return valid;
    }
    out_map = std::move(candidate);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::asset_internal
