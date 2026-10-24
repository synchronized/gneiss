// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/runtime_type_id.hpp"
#include <string>
gneiss::engine::runtime_type_id runtime_type_from_other_unit() {
  return gneiss::engine::runtime_type_of<std::string>();
}
