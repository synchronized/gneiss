// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/serialization/context.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
namespace gneiss::engine::serialization::detail {
result<writer::node, serialization::diagnostic> encode_value(const context&, writer&,
                                                             const value&) noexcept;
result<value, serialization::diagnostic> decode_value(const context&, reader&,
                                                      reader::node) noexcept;
}
