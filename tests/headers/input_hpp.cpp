// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/input.hpp>

#include <type_traits>

static_assert(std::is_nothrow_copy_constructible_v<gneiss::input_event>);
static_assert(!std::is_same_v<gneiss::input_event, gneiss_input_event>);
