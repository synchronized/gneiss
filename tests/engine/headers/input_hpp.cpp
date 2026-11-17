// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/input/types.hpp>

#include <type_traits>

static_assert(std::is_nothrow_copy_constructible_v<gneiss::input::event>);
static_assert(std::is_trivially_copyable_v<gneiss::input::event>);
