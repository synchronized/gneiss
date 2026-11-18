// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

namespace gneiss::meta {
struct type_ops;
namespace detail {
template <class T> const type_ops& ops_of();
}
}
