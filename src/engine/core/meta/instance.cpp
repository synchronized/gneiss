// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include <algorithm>
namespace gneiss::engine::meta {
result<instance> instance::from_object(registry_ref registry, engine::type_id id,
                                       const void* object) noexcept {
  if (!object)
    return error::invalid_argument;
  auto type = describe_type(registry, id);
  if (!type)
    return type.error();
  instance output;
  output.registry_ = std::move(registry);
  output.type_ = type->guid;
  output.cpp_type_ = id;
  output.kind_ = type_kind::object;
  output.data_ = object;
  return output;
}
result<std::vector<property_info>> instance::properties() const noexcept {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::object)
    return error::unsupported;
  auto type = describe_type(registry_, cpp_type_);
  if (!type)
    return type.error();
  return std::move(type->properties);
}
result<instance> instance::property(std::string_view name) const noexcept {
  auto entries = properties();
  if (!entries)
    return entries.error();
  auto found = std::ranges::find(*entries, name, &property_info::name);
  return found == entries->end() ? result<instance>{error::not_found} : property(found->id);
}
result<instance> instance::property(property_id id) const noexcept try {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::object)
    return error::unsupported;
  auto info = describe_property(registry_, type_, id);
  if (!info)
    return info.error();
  instance output;
  output.registry_ = registry_;
  output.parent_ = data_;
  output.parent_type_ = type_;
  output.property_ = id;
  output.type_ = info->type_guid;
  if (info->object_type) {
    auto binding = find_object_property(registry_, type_, id);
    if (!binding)
      return binding.error();
    if (!binding->object_getter)
      return error::unsupported;
    output.data_ = binding->object_getter(data_);
    if (!output.data_)
      return error::internal;
    output.cpp_type_ = info->object_type;
    output.kind_ = binding->access ? binding->access->kind : type_kind::object;
    output.access_ = binding->access;
  } else {
    if (info->kind == value_kind::invalid)
      return error::unsupported;
    output.kind_ = type_kind::scalar;
    output.scalar_kind_ = info->kind;
  }
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<value> instance::read_value() const noexcept try {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::scalar)
    return error::unsupported;
  if (access_ && access_->read) {
    auto output = access_->read(data_);
    if (output.kind() != scalar_kind_ || !valid_scalar(output))
      return error::internal;
    return output;
  }
  return read_property(registry_, parent_type_, property_, parent_);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<const void*> instance::object_data() const noexcept {
  if (kind_ == type_kind::invalid)
    return error::invalid_state;
  if (kind_ != type_kind::object)
    return error::unsupported;
  return data_;
}
result<mutable_instance> mutable_instance::from_object(registry_ref registry, engine::type_id id,
                                                       void* object) noexcept {
  auto view = instance::from_object(std::move(registry), id, object);
  if (!view)
    return view.error();
  mutable_instance output;
  output.view_ = std::move(*view);
  output.data_ = object;
  return output;
}
result<mutable_instance> mutable_instance::property(std::string_view name) const noexcept {
  auto entries = view_.properties();
  if (!entries)
    return entries.error();
  auto found = std::ranges::find(*entries, name, &property_info::name);
  return found == entries->end() ? result<mutable_instance>{error::not_found} : property(found->id);
}
result<mutable_instance> mutable_instance::property(property_id id) const noexcept try {
  auto child = view_.property(id);
  if (!child)
    return child.error();
  auto info = describe_property(view_.registry_, view_.type_, id);
  if (!info)
    return info.error();
  if (has_flags(info->flags, property_flags::read_only) ||
      !has_flags(info->capabilities, property_capabilities::writable))
    return error::unsupported;
  mutable_instance output;
  output.parent_ = data_;
  output.view_ = std::move(*child);
  if (info->object_type) {
    auto binding = find_object_property(view_.registry_, view_.type_, id);
    if (!binding)
      return binding.error();
    if (!binding->object_mutator)
      return error::unsupported;
    output.data_ = binding->object_mutator(data_);
    if (!output.data_)
      return error::internal;
    output.view_.data_ = output.data_;
  }
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error mutable_instance::write_value(const value& input) const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::scalar)
    return error::unsupported;
  if (view_.access_ && view_.access_->write) {
    if (input.kind() != view_.scalar_kind_ || !valid_scalar(input))
      return error::invalid_argument;
    view_.access_->write(data_, input);
    return error::success;
  }
  return write_property(view_.registry_, view_.parent_type_, view_.property_, parent_, input);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<void*> mutable_instance::object_data() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  return data_;
}
error mutable_instance::assign_object(engine::type_id id, void* source) const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  if (!source || id != view_.cpp_type_)
    return error::invalid_argument;
  if (source == data_)
    return error::success;
  if (view_.access_ && view_.access_->assign) {
    view_.access_->assign(data_, source);
    return error::success;
  }
  if (!parent_)
    return error::unsupported;
  auto binding = find_object_property(view_.registry_, view_.parent_type_, view_.property_);
  if (!binding)
    return binding.error();
  if (!binding->object_assign)
    return error::unsupported;
  binding->object_assign(parent_, source);
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<instance> instance::from_access(registry_ref registry, const detail::type_access& access,
                                       const void* data) noexcept {
  if (!data)
    return error::invalid_argument;
  instance out;
  if (access.kind == type_kind::object) {
    auto object = from_object(registry, access.id, data);
    if (!object)
      return object.error();
    out = std::move(*object);
  } else {
    out.registry_ = std::move(registry);
    out.data_ = data;
    out.type_ = access.guid;
    out.cpp_type_ = access.id;
    out.kind_ = access.kind;
    out.scalar_kind_ = access.scalar;
  }
  out.access_ = &access;
  return out;
}
result<mutable_instance> mutable_instance::from_access(registry_ref registry,
                                                       const detail::type_access& access,
                                                       void* data) noexcept {
  auto view = instance::from_access(std::move(registry), access, data);
  if (!view)
    return view.error();
  mutable_instance out;
  out.view_ = std::move(*view);
  out.data_ = data;
  return out;
}
result<sequence_view> instance::as_sequence() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::sequence)
    return error::unsupported;
  return sequence_view{*this};
}
result<mutable_sequence_view> mutable_instance::as_sequence() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::sequence)
    return error::unsupported;
  return mutable_sequence_view{*this};
}
result<associative_view> instance::as_associative() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::associative)
    return error::unsupported;
  return associative_view{*this};
}
result<mutable_associative_view> mutable_instance::as_associative() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::associative)
    return error::unsupported;
  return mutable_associative_view{*this};
}
result<optional_view> instance::as_optional() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::optional)
    return error::unsupported;
  return optional_view{*this};
}
result<mutable_optional_view> mutable_instance::as_optional() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::optional)
    return error::unsupported;
  return mutable_optional_view{*this};
}
}
