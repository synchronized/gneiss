// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/serialization/value.hpp"
#include "engine/core/meta/associative_view.hpp"
#include "engine/core/meta/detail/property_view_access.hpp"
#include "engine/core/meta/detail/reference_view_access.hpp"
#include "engine/core/meta/optional_view.hpp"
#include "engine/core/meta/reference_view.hpp"
#include "engine/core/meta/sequence_view.hpp"
#include "engine/core/meta/variant_view.hpp"
#include "engine/core/serialization/detail/context_access.hpp"
#include "engine/core/serialization/detail/traversal.hpp"
#include <algorithm>
#include <unordered_set>
namespace gneiss::engine::serialization {
namespace {
using namespace meta;
using property_access = meta::detail::property_view_access;
using reference_access = meta::detail::reference_view_access;
struct capture_state {
  const context& ctx;
  std::unordered_set<const void*> active;
  diagnostic failure;
  result<engine::value> read(const instance& source, std::size_t depth) {
    auto result = read_impl(source, depth);
    if (!result && failure.error.ok())
      failure = {.path = std::string(ctx.property_path()), .error = result.error()};
    return result;
  }
  result<engine::value> read_impl(const instance& source, std::size_t depth) {
    if (depth > ctx.maximum_depth())
      return error::invalid_argument;
    switch (source.kind()) {
    case type_kind::empty:
      return engine::value{};
    case type_kind::scalar:
      return source.read_value();
    case type_kind::object: {
      auto policy = find_serialization(property_access::registry(source), source.type_id());
      if (policy && !policy->reflected && (policy->encode || policy->decode))
        return error::unsupported;
      record_value_data record{source.guid(), {}};
      auto fields = source.properties();
      if (!fields)
        return fields.error();
      for (const auto& field : *fields) {
        if (!has_flags(field.capabilities, property_capabilities::writable))
          continue;
        detail::path_scope path{ctx, "/properties/" + std::to_string(field.id.get())};
        auto child = source.property(field.id);
        if (!child)
          return child.error();
        auto item = read(*child, depth + 1);
        if (!item)
          return item.error();
        record.properties.push_back({field.id.get(), std::move(*item)});
      }
      std::ranges::sort(record.properties, {}, &value_property::id);
      return engine::value{record_value{record}};
    }
    case type_kind::sequence: {
      auto view = source.as_sequence();
      if (!view)
        return view.error();
      auto count = view->size();
      if (!count)
        return count.error();
      sequence_value_data data;
      data.fixed = view->is_fixed_size();
      data.keyed = view->has_identity();
      data.elements.reserve(*count);
      for (std::size_t i = 0; i < *count; ++i) {
        detail::path_scope path{ctx, "/indices/" + std::to_string(i)};
        auto child = view->at(i);
        if (!child)
          return child.error();
        auto item = read(*child, depth + 1);
        if (!item)
          return item.error();
        data.elements.push_back(std::move(*item));
        if (data.keyed) {
          auto id = view->identity(i);
          if (!id)
            return id.error();
          data.identities.push_back(*id);
        }
      }
      return engine::value{sequence_value{data}};
    }
    case type_kind::associative: {
      auto view = source.as_associative();
      if (!view)
        return view.error();
      auto keys = view->keys();
      if (!keys)
        return keys.error();
      std::ranges::sort(*keys);
      map_value_data data;
      for (const auto& key : *keys) {
        detail::path_scope path{ctx, "/keys/" + std::to_string(key.size()) + ":" + key};
        auto child = view->find(key);
        if (!child)
          return child.error();
        auto item = read(*child, depth + 1);
        if (!item)
          return item.error();
        data.entries.push_back({engine::value{key}, std::move(*item)});
      }
      return engine::value{map_value{data}};
    }
    case type_kind::optional: {
      auto view = source.as_optional();
      if (!view)
        return view.error();
      auto present = view->has_value();
      if (!present)
        return present.error();
      optional_value_data data;
      if (*present) {
        auto child = view->get();
        if (!child)
          return child.error();
        auto item = read(*child, depth + 1);
        if (!item)
          return item.error();
        data.content = std::move(*item);
      }
      return engine::value{optional_value{data}};
    }
    case type_kind::variant: {
      auto view = source.as_variant();
      if (!view)
        return view.error();
      auto index = view->index();
      if (!index)
        return index.error();
      if (view->names().size() != view->alternative_count())
        return error::unsupported;
      auto child = view->value();
      if (!child)
        return child.error();
      auto item = read(*child, depth + 1);
      if (!item)
        return item.error();
      return engine::value{
          variant_value{variant_value_data{std::string(view->names()[*index]), std::move(*item)}}};
    }
    case type_kind::reference: {
      auto view = source.as_reference();
      if (!view)
        return view.error();
      auto present = view->has_value();
      if (!present)
        return present.error();
      if (!*present)
        return engine::value{};
      auto target_type = source.type().element_type();
      if (!target_type)
        return target_type.error();
      auto binding = find_serialization(property_access::registry(source), target_type->id());
      auto owner = reference_access::owner(source);
      if (!owner)
        return owner.error();
      if (binding && binding->capture_reference) {
        auto ref = binding->capture_reference(target_type->guid(), owner->get());
        return ref ? result<engine::value>{engine::value{*ref}}
                   : result<engine::value>{ref.error()};
      }
      // 自定义外部引用不能静默按内嵌对象复制。
      if (binding && binding->encode_reference)
        return error::unsupported;
      if (!active.insert(owner->get()).second)
        return error::invalid_argument;
      struct guard {
        std::unordered_set<const void*>& active;
        const void* pointer;
        ~guard() { active.erase(pointer); }
      } cleanup{active, owner->get()};
      auto child = view->get();
      return child ? read(*child, depth + 1) : result<engine::value>{child.error()};
    }
    default:
      return error::unsupported;
    }
  }
};
struct restore_state {
  const context& ctx;
  diagnostic failure;
  error write(const mutable_instance& target, const engine::value& data, std::size_t depth,
              bool sequence_candidate = false) {
    auto status = write_impl(target, data, depth, sequence_candidate);
    if (status.failed() && failure.error.ok())
      failure = {.path = std::string(ctx.property_path()), .error = status};
    return status;
  }
  error write_impl(const mutable_instance& target, const engine::value& data, std::size_t depth,
                   bool sequence_candidate) {
    if (depth > ctx.maximum_depth())
      return error::invalid_argument;
    if (property_access::requires_replacement(target))
      return property_access::replace(
          target, [&](const mutable_instance& temp) { return write(temp, data, depth); });
    switch (target.kind()) {
    case type_kind::empty:
      return data.kind() == value_kind::invalid ? error::success : error::invalid_argument;
    case type_kind::scalar:
      return target.write_value(data);
    case type_kind::object: {
      auto record = std::get_if<record_value>(&data.payload);
      if (!record || record->get().type != target.type().guid())
        return error::invalid_argument;
      std::unordered_set<std::uint32_t> seen;
      for (const auto& field : record->get().properties) {
        if (!field.id || !seen.insert(field.id).second)
          return error::invalid_argument;
        detail::path_scope path{ctx, "/properties/" + std::to_string(field.id)};
        auto child = target.property(property_id{field.id});
        if (!child)
          return child.error();
        auto status = write(*child, field.content, depth + 1);
        if (status.failed())
          return status;
      }
      auto fields = target.as_const().properties();
      if (!fields)
        return fields.error();
      for (const auto& field : *fields)
        if (has_flags(field.capabilities, property_capabilities::writable) &&
            !seen.contains(field.id.get()))
          return error::invalid_argument;
      auto binding = find_serialization(property_access::registry(target), target.type().id());
      if (binding && binding->validate) {
        auto address = target.object_data();
        if (!address)
          return address.error();
        return binding->validate(*address).error;
      }
      return error::success;
    }
    case type_kind::sequence: {
      auto data_view = std::get_if<sequence_value>(&data.payload);
      if (!data_view)
        return error::invalid_argument;
      auto view = target.as_sequence();
      if (!view)
        return view.error();
      if (!sequence_candidate && (data_view->get().fixed != view->is_fixed_size() ||
                                  data_view->get().keyed != view->has_identity()))
        return error::invalid_argument;
      if (view->supports_replacement()) {
        auto status = view->replace(
            [&](const mutable_instance& temp) { return write(temp, data, depth, true); });
        if (status.failed())
          return status;
        for (std::size_t i = 0; i < data_view->get().identities.size(); ++i) {
          auto identity = view->identity(i);
          if (!identity || *identity != data_view->get().identities[i])
            return error::invalid_argument;
        }
        return error::success;
      }
      const auto& elements = data_view->get().elements;
      auto count = view->size();
      if (!count)
        return count.error();
      if (*count != elements.size()) {
        auto status = view->resize(elements.size());
        if (status.failed())
          return status;
      }
      for (std::size_t i = 0; i < elements.size(); ++i) {
        detail::path_scope path{ctx, "/indices/" + std::to_string(i)};
        auto child = view->at(i);
        if (!child)
          return child.error();
        auto status = write(*child, elements[i], depth + 1);
        if (status.failed())
          return status;
      }
      if (view->has_identity())
        for (std::size_t i = 0; i < data_view->get().identities.size(); ++i) {
          auto id = view->identity(i);
          if (!id || *id != data_view->get().identities[i])
            return error::invalid_argument;
        }
      return error::success;
    }
    case type_kind::associative: {
      auto entries = std::get_if<map_value>(&data.payload);
      if (!entries)
        return error::invalid_argument;
      auto view = target.as_associative();
      if (!view)
        return view.error();
      auto status = view->clear();
      if (status.failed())
        return status;
      std::unordered_set<std::string> seen;
      for (const auto& entry : entries->get().entries) {
        auto key = std::get_if<std::string>(&entry.key.payload);
        if (!key || !seen.insert(*key).second)
          return error::invalid_argument;
        detail::path_scope path{ctx, "/keys/" + std::to_string(key->size()) + ":" + *key};
        auto child = view->insert(*key);
        if (!child)
          return child.error();
        status = write(*child, entry.content, depth + 1);
        if (status.failed())
          return status;
      }
      return error::success;
    }
    case type_kind::optional: {
      auto item = std::get_if<optional_value>(&data.payload);
      if (!item)
        return error::invalid_argument;
      auto view = target.as_optional();
      if (!view)
        return view.error();
      if (!item->get().content)
        return view->reset();
      auto child = view->ensure();
      return child ? write(*child, *item->get().content, depth + 1) : child.error();
    }
    case type_kind::variant: {
      auto item = std::get_if<variant_value>(&data.payload);
      if (!item)
        return error::invalid_argument;
      auto view = target.as_variant();
      if (!view)
        return view.error();
      auto name = std::ranges::find(view->names(), item->get().alternative);
      if (name == view->names().end())
        return error::invalid_argument;
      auto status = view->select(std::size_t(name - view->names().begin()));
      if (status.failed())
        return status;
      auto child = view->value();
      return child ? write(*child, item->get().content, depth + 1) : child.error();
    }
    case type_kind::reference: {
      if (data.kind() == value_kind::invalid)
        return reference_access::reset(target);
      const auto& registry = property_access::registry(target);
      if (auto asset = std::get_if<asset_reference_value>(&data.payload)) {
        auto type = find_type(registry, asset->type);
        auto element = target.type().element_type();
        if (!type || !element || type->id() != element->id())
          return error::invalid_argument;
        if (asset->uri.empty())
          return reference_access::reset(target);
        if (!ctx.references())
          return error::invalid_state;
        auto owner = ctx.references()->resolve(type->id(), asset->uri, ctx.property_path());
        if (!owner)
          return owner.error().error;
        return reference_access::assign(target, type->id(), *owner);
      }
      auto record = std::get_if<record_value>(&data.payload);
      if (!record)
        return error::invalid_argument;
      auto type = find_type(registry, record->get().type);
      if (!type)
        return type.error();
      auto ops = find_object_ops(registry, type->id());
      if (!ops || !ops->construct)
        return error::unsupported;
      auto element = target.type().element_type();
      if (!element)
        return element.error();
      auto compatible = detail::cast_base(registry, type->id(), element->id(), nullptr);
      if (!compatible)
        return compatible.error().error;
      auto owner = ops->construct();
      if (!owner)
        return error::out_of_memory;
      auto child = mutable_instance::from_object(registry, type->id(), owner.get());
      if (!child)
        return child.error();
      auto status = write(*child, data, depth + 1);
      if (status.failed())
        return status;
      auto adjusted = detail::cast_base(registry, type->id(), element->id(), owner.get());
      if (!adjusted)
        return adjusted.error().error;
      return reference_access::assign(target, element->id(), owner.alias(*adjusted));
    }
    default:
      return error::unsupported;
    }
  }
};
}
result<engine::value, diagnostic> capture_value(const context& ctx,
                                                const meta::instance& source) noexcept try {
  detail::operation_scope operation{ctx};
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  if (*registry != property_access::registry(source))
    return diagnostic{.error = error::invalid_argument};
  capture_state state{ctx, {}, {}};
  auto captured = state.read(source, 0);
  if (!captured)
    return state.failure;
  auto status = validate_persistent_value(*captured, ctx.maximum_depth());
  if (status.failed())
    return diagnostic{.error = status};
  return std::move(*captured);
} catch (const std::bad_alloc&) {
  return diagnostic{.error = error::out_of_memory};
} catch (...) {
  return diagnostic{.error = error::internal};
}
diagnostic apply_value(const context& ctx, const meta::mutable_instance& target,
                       const engine::value& data) noexcept try {
  detail::operation_scope operation{ctx};
  auto registry = ctx.registry();
  if (!registry)
    return {.error = registry.error()};
  if (*registry != property_access::registry(target))
    return {.error = error::invalid_argument};
  auto status = validate_persistent_value(data, ctx.maximum_depth());
  if (status.failed())
    return {.error = status};
  restore_state state{ctx, {}};
  status = state.write(target, data, 0);
  return status.failed() ? state.failure : diagnostic{};
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
result<engine::value, diagnostic> capture_value(const meta::instance& source) noexcept {
  context ctx{property_access::registry(source)};
  return capture_value(ctx, source);
}
diagnostic apply_value(const meta::mutable_instance& target, const engine::value& data) noexcept {
  context ctx{property_access::registry(target)};
  return apply_value(ctx, target, data);
}
}
