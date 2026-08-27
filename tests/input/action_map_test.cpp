// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "input/action_map.h"

int main() {
  gneiss::input_internal::action_map map;
  constexpr auto valid =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":-1.0},{"key":7,"scale":1.0}]}]})";
  if (gneiss::input_internal::parse_action_map(valid, map) != GNEISS_SUCCESS ||
      map.actions.size() != 1U || map.actions[0].bindings.size() != 2U) {
    return 1;
  }
  const auto original_size = map.actions[0].bindings.size();
  constexpr auto duplicate =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":1}]},{"name":"move","bindings":[{"key":7,"scale":1}]}]})";
  constexpr auto unknown_key =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":256,"scale":1}]}]})";
  constexpr auto invalid_scale =
      R"({"format":"gneiss.input-map","version":1,"actions":[{"name":"move","bindings":[{"key":4,"scale":0}]}]})";
  if (gneiss::input_internal::parse_action_map(duplicate, map) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss::input_internal::parse_action_map(unknown_key, map) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss::input_internal::parse_action_map(invalid_scale, map) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      map.actions[0].bindings.size() != original_size) {
    return 2;
  }
  return 0;
}
