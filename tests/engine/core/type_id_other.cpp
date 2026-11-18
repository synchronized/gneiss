// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/base/type_id.hpp>
#include <string>
gneiss::type_id runtime_type_from_other_unit() {
  return gneiss::runtime_type_of<std::string>();
}
