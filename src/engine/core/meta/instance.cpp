// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include <algorithm>
namespace gneiss::engine::meta {
bool type::is_fixed_sequence() const noexcept {
  const auto* ops = access_ ? access_->get<sequence_ops>() : nullptr;
  return ops && ops->fixed_size;
}
bool type::has_sequence_identity() const noexcept {
  const auto* ops = access_ ? access_->get<sequence_ops>() : nullptr;
  return ops && ops->identity;
}
type type::element_type() const noexcept {
  if (!access_ || !access_->element_ops())
    return {};
  return detail::resolve_type(registry_, access_->element());
}

result<const_instance> const_instance::from_object(registry_ref registry, engine::type_id id,
                                                   const void* object) noexcept {
  if (!object)
    return error::invalid_argument;
  auto frozen = is_frozen(registry);
  if (!frozen)
    return frozen.error();
  if (!*frozen)
    return error::not_ready;
  auto type = find_type(registry, id);
  if (!type)
    return error::not_found;
  if (type.kind() != type_kind::object)
    return error::not_found;
  const_instance output;
  output.type_ = std::move(type);
  output.data_ = object;
  return output;
}
property_range const_instance::properties() const noexcept { return type().properties(); }
result<const_instance> const_instance::member(std::string_view name) const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  auto found = type().find_property(name);
  return found ? found.get(*this) : result<const_instance>{error::not_found};
}
result<const_instance> const_instance::member(property_id id) const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  auto info = type().find_property(id);
  if (!info)
    return error::not_found;
  if (info.declaring_type_id() != type_id()) {
    auto ops = find_object_ops(registry(), type_id());
    if (!ops || !ops->upcast)
      return ops ? error::invalid_state : ops.error();
    // upcast 仅调整地址，不修改对象；随后继续使用只读视图。
    auto base = from_object(registry(), ops->base_type, ops->upcast(const_cast<void*>(data_)));
    return base ? base->member(id) : result<const_instance>{base.error()};
  }
  const_instance output;
  output.parent_ = data_;
  output.parent_type_ = type_id();
  output.property_ = id;
  output.type_ = info.type();
  if (info.type().kind() != type_kind::scalar && info.type().kind() != type_kind::invalid) {
    auto binding = find_object_property(registry(), type_id(), id);
    if (!binding)
      return binding.error();
    if (!binding->object_getter)
      return error::unsupported;
    output.data_ = binding->object_getter(data_);
    if (!output.data_)
      return error::internal;
  } else {
    if (info.type().scalar_kind() == value_kind::invalid)
      return error::unsupported;
  }
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<value> const_instance::read_value() const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() == type_kind::variant && type_id() == runtime_type_of<value>() && data_)
    return *static_cast<const value*>(data_);
  if (kind() == type_kind::reference && access()) {
    const auto* ops = access()->get<reference_ops>();
    return ops && ops->read_object ? result<value>{value{ops->read_object(data_)}}
                                   : result<value>{error::unsupported};
  }
  if (kind() != type_kind::scalar)
    return error::unsupported;
  if (data_ && access() && access()->get<scalar_ops>()->read) {
    auto output = access()->get<scalar_ops>()->read(data_);
    if (output.kind() != scalar_kind() || !valid_scalar(output))
      return error::internal;
    return output;
  }
  return read_property(registry(), parent_type_, property_, parent_);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<const void*> const_instance::object_data() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  return data_;
}
result<instance> instance::from_object(registry_ref registry, engine::type_id id,
                                       void* object) noexcept {
  auto view = const_instance::from_object(std::move(registry), id, object);
  if (!view)
    return view.error();
  instance output;
  output.view_ = std::move(*view);
  output.data_ = object;
  return output;
}
result<instance> instance::member(std::string_view name) const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  auto found = view_.type().find_property(name);
  return found ? found.get(*this) : result<instance>{error::not_found};
}
result<instance> instance::member(property_id id) const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (!data_)
    return error::unsupported;
  auto child = view_.member(id);
  if (!child)
    return child.error();
  auto info = view_.type().find_property(id);
  if (!info)
    return error::not_found;
  if (info.declaring_type_id() != view_.type_id()) {
    auto ops = find_object_ops(view_.registry(), view_.type_id());
    if (!ops || !ops->upcast)
      return ops ? error::invalid_state : ops.error();
    auto base = from_object(view_.registry(), ops->base_type, ops->upcast(data_));
    return base ? base->member(id) : result<instance>{base.error()};
  }
  if (!has_flags(info.capabilities(), property_capabilities::writable))
    return error::unsupported;
  instance output;
  output.parent_ = data_;
  output.view_ = std::move(*child);
  if (info.type().kind() != type_kind::scalar && info.type().kind() != type_kind::invalid) {
    auto binding = find_object_property(view_.registry(), view_.type_id(), id);
    if (!binding)
      return binding.error();
    if (binding->object_setter)
      return output;
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
error instance::assign(const value& input) const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() == type_kind::variant && view_.type_id() == runtime_type_of<value>() && data_) {
    *static_cast<value*>(data_) = input;
    return error::success;
  }
  if (kind() == type_kind::reference && view_.access()) {
    const auto* ops = view_.access()->get<reference_ops>();
    if (!data_ || !ops || !ops->write_object)
      return error::unsupported;
    const auto* object = std::get_if<ref<meta::object>>(&input.payload);
    return object ? ops->write_object(data_, *object) : error::invalid_argument;
  }
  if (kind() != type_kind::scalar)
    return error::unsupported;
  if (data_ && view_.access() && view_.access()->get<scalar_ops>()->write) {
    if (input.kind() != view_.scalar_kind() || !valid_scalar(input))
      return error::invalid_argument;
    return view_.access()->get<scalar_ops>()->write(data_, input);
  }
  return write_property(view_.registry(), view_.parent_type_, view_.property_, parent_, input);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<void*> instance::object_data() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object || !data_)
    return error::unsupported;
  return data_;
}
error instance::assign(const const_instance& source) const noexcept try {
  if (kind() == type_kind::invalid || source.kind() == type_kind::invalid)
    return error::invalid_state;
  if (requires_replacement()) {
    if (view_.type_id() != source.type_id())
      return error::invalid_argument;
    auto binding = find_object_property(view_.registry(), view_.parent_type_, view_.property_);
    if (!binding)
      return binding.error();
    return binding->object_setter(parent_, source.data_);
  }
  if ((kind() != type_kind::object && kind() != type_kind::reference) || source.kind() != kind())
    return error::unsupported;
  if (view_.type_id() != source.type_id())
    return error::invalid_argument;
  if (!view_.access() || !view_.access()->copy_assign)
    return error::unsupported;
  if (data_ != source.data_)
    view_.access()->copy_assign(data_, source.data_);
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error instance::assign_object(engine::type_id id, void* source) const noexcept try {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (requires_replacement()) {
    if (!source || id != view_.type_id())
      return error::invalid_argument;
    auto binding = find_object_property(view_.registry(), view_.parent_type_, view_.property_);
    if (!binding)
      return binding.error();
    return binding->object_setter(parent_, source);
  }
  if (kind() != type_kind::object)
    return error::unsupported;
  if (!source || id != view_.type_id())
    return error::invalid_argument;
  if (source == data_)
    return error::success;
  if (view_.access() && view_.access()->move_assign) {
    view_.access()->move_assign(data_, source);
    return error::success;
  }
  if (!parent_)
    return error::unsupported;
  auto binding = find_object_property(view_.registry(), view_.parent_type_, view_.property_);
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

result<const_instance> const_instance::from_access(registry_ref registry,
                                                   const detail::type_ops& access,
                                                   const void* data) noexcept {
  if (!data)
    return error::invalid_argument;
  auto description = detail::resolve_type(registry, access);
  if (!description)
    return error::invalid_state;
  const_instance out;
  out.data_ = data;
  out.type_ = std::move(description);
  return out;
}
result<instance> instance::from_access(registry_ref registry, const detail::type_ops& access,
                                       void* data) noexcept {
  auto view = const_instance::from_access(std::move(registry), access, data);
  if (!view)
    return view.error();
  instance out;
  out.view_ = std::move(*view);
  out.data_ = data;
  return out;
}
result<sequence_view> const_instance::as_sequence() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::sequence)
    return error::unsupported;
  return sequence_view{*this};
}
result<mutable_sequence_view> instance::as_sequence() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (!data_)
    return error::unsupported;
  if (kind() != type_kind::sequence)
    return error::unsupported;
  return mutable_sequence_view{*this};
}
result<associative_view> const_instance::as_associative() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::associative)
    return error::unsupported;
  return associative_view{*this};
}
result<mutable_associative_view> instance::as_associative() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (!data_)
    return error::unsupported;
  if (kind() != type_kind::associative)
    return error::unsupported;
  return mutable_associative_view{*this};
}
result<optional_view> const_instance::as_optional() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::optional)
    return error::unsupported;
  return optional_view{*this};
}
result<mutable_optional_view> instance::as_optional() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (!data_)
    return error::unsupported;
  if (kind() != type_kind::optional)
    return error::unsupported;
  return mutable_optional_view{*this};
}
result<reference_view> const_instance::as_reference() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::reference)
    return error::unsupported;
  return reference_view{*this};
}
result<mutable_reference_view> instance::as_reference() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (!data_)
    return error::unsupported;
  if (kind() != type_kind::reference)
    return error::unsupported;
  return mutable_reference_view{*this};
}
}

namespace gneiss::engine::meta {
error instance::replace(const std::function<error(const instance&)>& initialize) const noexcept
    try {
  const auto& target = *this;
  if (!requires_replacement() || !initialize)
    return error::invalid_argument;
  auto binding = find_object_property(target.view_.registry(), target.view_.parent_type_,
                                      target.view_.property_);
  if (!binding)
    return binding.error();
  auto storage = binding->object_construct();
  if (!storage)
    return error::out_of_memory;
  auto temporary = instance::from_access(target.view_.registry(), *binding->access, storage.get());
  if (!temporary)
    return temporary.error();
  auto status = initialize(*temporary);
  return status.failed() ? status : binding->object_setter(target.parent_, storage.get());
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
}

namespace gneiss::engine::meta {
result<variant_view> const_instance::as_variant() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_argument;
  if (kind() != type_kind::variant)
    return error::unsupported;
  return variant_view{*this};
}
result<mutable_variant_view> instance::as_variant() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_argument;
  if (!data_ || kind() != type_kind::variant)
    return error::unsupported;
  return mutable_variant_view{*this};
}
}

namespace gneiss::engine::meta {
result<const_instance> property::get(const const_instance& object) const noexcept {
  if (!record_)
    return error::invalid_state;
  if (registry_ != object.registry())
    return error::invalid_argument;
  auto info = object.type().find_property(id());
  if (!info)
    return error::not_found;
  if (info.declaring_type_id() != declaring_type_id() || info.declaring_type() != declaring_type())
    return error::invalid_argument;
  return object.member(id());
}
result<instance> property::get(const instance& object) const noexcept {
  if (!record_)
    return error::invalid_state;
  const auto& view = object.as_const();
  if (registry_ != view.registry())
    return error::invalid_argument;
  auto info = view.type().find_property(id());
  if (!info)
    return error::not_found;
  if (info.declaring_type_id() != declaring_type_id() || info.declaring_type() != declaring_type())
    return error::invalid_argument;
  return object.member(id());
}
}
