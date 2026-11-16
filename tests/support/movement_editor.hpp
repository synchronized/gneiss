// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/components/component_registration.hpp"
#include "movement_component.hpp"
namespace gneiss::test_support {
inline editor::component_editor_adapter movement_editor() {
  return {.type = movement_description::type_guid,
          .create_default = []() -> e::result<e::component_description> {
            return e::make_component_description(movement_description{});
          },
          .removable = true};
}
}
