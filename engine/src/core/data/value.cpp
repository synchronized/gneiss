// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <algorithm>
#include <cmath>
#include <gneiss/engine/core/data/value.hpp>
#include <gneiss/engine/core/utf8.hpp>
#include <unordered_set>
namespace gneiss {
namespace {
error validate(const value& input, std::size_t depth, std::size_t maximum) {
  if (depth > maximum || input.payload.valueless_by_exception())
    return error::invalid_argument;
  auto child = [&](const value& item) { return validate(item, depth + 1, maximum); };
  return std::visit(
      [&](const auto& data) -> error {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, ref<meta::object>>)
          return error::unsupported;
        else if constexpr (std::is_same_v<T, std::string>)
          return utf8::is_valid(data) ? error::success : error::invalid_argument;
        else if constexpr (std::is_floating_point_v<T>)
          return std::isfinite(data) ? error::success : error::invalid_argument;
        else if constexpr (requires { math::components(data); })
          return std::ranges::all_of(math::components(data),
                                     [](float v) { return std::isfinite(v); })
                     ? error::success
                     : error::invalid_argument;
        else if constexpr (std::is_same_v<T, record_value>) {
          if (!data.get().type)
            return error::invalid_argument;
          std::unordered_set<std::uint32_t> seen;
          for (const auto& field : data.get().properties) {
            if (!field.id || !seen.insert(field.id).second)
              return error::invalid_argument;
            auto status = child(field.content);
            if (status.failed())
              return status;
          }
          return error::success;
        } else if constexpr (std::is_same_v<T, sequence_value>) {
          const auto& sequence = data.get();
          if (sequence.keyed) {
            if (sequence.identities.size() != sequence.elements.size())
              return error::invalid_argument;
            std::unordered_set<guid, guid_hash> ids;
            for (auto id : sequence.identities)
              if (!id || !ids.insert(id).second)
                return error::invalid_argument;
          } else if (!sequence.identities.empty())
            return error::invalid_argument;
          for (const auto& element : sequence.elements) {
            auto status = child(element);
            if (status.failed())
              return status;
          }
          return error::success;
        } else if constexpr (std::is_same_v<T, map_value>) {
          const auto& entries = data.get().entries;
          for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            switch (entry.key.kind()) {
            case value_kind::boolean:
            case value_kind::int64:
            case value_kind::uint64:
            case value_kind::string:
            case value_kind::guid:
              break;
            default:
              return error::invalid_argument;
            }
            if (child(entry.key).failed())
              return error::invalid_argument;
            for (std::size_t j = 0; j < i; ++j)
              if (entries[j].key == entry.key)
                return error::invalid_argument;
            auto status = child(entry.content);
            if (status.failed())
              return status;
          }
          return error::success;
        } else if constexpr (std::is_same_v<T, optional_value>) {
          if (data.get().content)
            return child(*data.get().content);
          return error::success;
        } else if constexpr (std::is_same_v<T, variant_value>) {
          if (data.get().alternative.empty() || !utf8::is_valid(data.get().alternative))
            return error::invalid_argument;
          return child(data.get().content);
        } else if constexpr (std::is_same_v<T, asset_reference_value>) {
          if (!data.type || !utf8::is_valid(data.uri))
            return error::invalid_argument;
          return error::success;
        } else {
          return error::success;
        }
      },
      input.payload);
}
}
error validate_persistent_value(const value& data, std::size_t depth) {
  return validate(data, 0, depth);
}
}
