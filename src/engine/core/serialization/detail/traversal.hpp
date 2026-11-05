// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/detail/context_access.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
namespace gneiss::engine::serialization::detail {
using meta::registry_ref;

result<writer::node, serialization::diagnostic>
encode_view(const context&, const registry_ref&, const meta::instance&, writer*, std::size_t);
result<writer::node, serialization::diagnostic>
encode_nested(const context&, const registry_ref&, type_id, const void*, writer*, std::size_t);
result<writer::node, serialization::diagnostic>
encode_members(const context&, const registry_ref&, type_id, const void*, writer*, std::size_t);
serialization::diagnostic decode_view(const context&, const registry_ref&,
                                      const meta::mutable_instance&, reader::node, std::size_t);
result<ref<void>, serialization::diagnostic> decode_owned(const context&, const registry_ref&,
                                                          type_id, reader::node, std::size_t,
                                                          bool root = false);
serialization::diagnostic decode_members(const context&, const registry_ref&, type_id, void*,
                                         reader::node, std::size_t,
                                         std::span<const std::string_view> = {});
}
