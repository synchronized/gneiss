// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/input.hpp>

#include <type_traits>

static_assert(std::is_standard_layout_v<gneiss::input_event>);
static_assert(sizeof(gneiss::input_event) == sizeof(gneiss_input_event));
