// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
#include <unordered_set>
namespace gneiss::engine::serialization::detail {
using meta::registry_ref;
using meta::serialization_diagnostic;
struct traversal_context {
  std::unordered_set<const void*> active{};
  reader* input{};
};
result<writer::node, serialization_diagnostic>
encode_view(traversal_context&, const registry_ref&, const meta::instance&, writer*, std::size_t);
result<writer::node, serialization_diagnostic>
encode_nested(traversal_context&, const registry_ref&, type_id, const void*, writer*, std::size_t);
result<writer::node, serialization_diagnostic>
encode_members(traversal_context&, const registry_ref&, type_id, const void*, writer*, std::size_t);
serialization_diagnostic decode_view(traversal_context&, const registry_ref&,
                                     const meta::mutable_instance&, reader::node, std::size_t);
result<ref<void>, serialization_diagnostic> decode_owned(traversal_context&, const registry_ref&,
                                                         type_id, reader::node, std::size_t,
                                                         bool root = false);
serialization_diagnostic decode_members(traversal_context&, const registry_ref&, type_id, void*,
                                        reader::node, std::size_t,
                                        std::span<const std::string_view> = {});
}
