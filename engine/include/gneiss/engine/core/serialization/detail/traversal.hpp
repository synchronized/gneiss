// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/serialization/detail/context_access.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/serialization/reader.hpp>
#include <gneiss/engine/core/serialization/writer.hpp>

namespace gneiss::serialization::detail {
using meta::registry_ref;

result<void*, serialization::diagnostic> cast_base(const registry_ref&, type_id actual,
                                                   type_id declared, void* pointer);

result<writer::node, serialization::diagnostic>
encode_view(const write_context&, const registry_ref&, const meta::const_instance&, writer*,
            std::size_t, const reference_policy* = nullptr);
result<writer::node, serialization::diagnostic> encode_nested(const write_context&,
                                                              const registry_ref&, type_id,
                                                              const void*, writer*, std::size_t);
result<writer::node, serialization::diagnostic> encode_members(const write_context&,
                                                               const registry_ref&, type_id,
                                                               const void*, writer*, std::size_t);
serialization::diagnostic decode_view(const read_context&, const registry_ref&,
                                      const meta::instance&, reader::node, std::size_t,
                                      const reference_policy* = nullptr);
result<ref<void>, serialization::diagnostic> decode_owned(const read_context&, const registry_ref&,
                                                          type_id, reader::node, std::size_t,
                                                          bool root = false);
serialization::diagnostic decode_members(const read_context&, const registry_ref&, type_id, void*,
                                         reader::node, std::size_t,
                                         std::span<const std::string_view> = {});
}
