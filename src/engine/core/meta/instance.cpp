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
type type::element_type() const {
  if (!access_ || !access_->element_ops())
    return {};
  return detail::resolve_type(registry_, access_->element());
}

template <bool Mutable>
result<basic_instance<Mutable>>
basic_instance<Mutable>::from_object(registry_ref registry, engine::type_id id, pointer object) {
  if (!object)
    return error::invalid_argument;
  auto frozen = is_frozen(registry);
  if (!frozen)
    return frozen.error();
  if (!*frozen)
    return error::not_ready;
  auto description = find_type(registry, id);
  if (!description || description.kind() != type_kind::object)
    return error::not_found;
  basic_instance output;
  output.type_ = std::move(description);
  output.data_ = object;
  return output;
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_instance<Mutable>::from_access(registry_ref registry,
                                                                     const detail::type_ops& access,
                                                                     pointer data) {
  if (!data)
    return error::invalid_argument;
  basic_instance output;
  output.type_ = detail::resolve_type(registry, access);
  if (!output.type_)
    return error::invalid_state;
  output.data_ = data;
  return output;
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_instance<Mutable>::member(std::string_view name) const {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  const auto field = type_.find_property(name);
  return field ? member(field.id()) : result<basic_instance>{error::not_found};
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_instance<Mutable>::member(property_id id) const {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object || !data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!writable_)
      return error::unsupported;
  const auto field = type_.find_property(id);
  if (!field)
    return error::not_found;
  if (field.declaring_type_id() != type_id()) {
    auto ops = find_object_ops(registry(), type_id());
    if (!ops || !ops->upcast)
      return ops ? error::invalid_state : ops.error();
    auto base = from_object(registry(), ops->base_type, ops->upcast(const_cast<void*>(data_)));
    return base ? base->member(id) : result<basic_instance>{base.error()};
  }
  if constexpr (Mutable)
    if (!has_flags(field.capabilities(), property_capabilities::writable))
      return error::unsupported;
  basic_instance output;
  output.type_ = field.type();
  output.parent_ = data_;
  output.parent_type_ = type_id();
  output.property_ = id;
  if (output.kind() == type_kind::scalar) {
    if (output.scalar_kind() == value_kind::invalid)
      return error::unsupported;
    return output;
  }
  auto binding = find_object_property(registry(), type_id(), id);
  if (!binding)
    return binding.error();
  if (!binding->object_getter)
    return error::unsupported;
  output.data_ = binding->object_getter(data_);
  if constexpr (Mutable) {
    if (binding->object_setter)
      output.writable_ = false;
    else {
      if (!binding->object_mutator)
        return error::unsupported;
      output.data_ = binding->object_mutator(mutable_data());
    }
  }
  if (!output.data_)
    return error::internal;
  return output;
}
template <bool Mutable>
result<typename basic_instance<Mutable>::pointer>
basic_instance<Mutable>::object_data() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::object)
    return error::unsupported;
  if constexpr (Mutable) {
    if (!mutable_data())
      return error::unsupported;
    return mutable_data();
  } else
    return data_;
}
template <bool Mutable> result<value> basic_instance<Mutable>::read_value() const {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() == type_kind::variant && type_id() == runtime_type_of<value>() && data_)
    return *static_cast<const value*>(data_);
  if (kind() == type_kind::reference && access()) {
    const auto* ops = access()->template get<reference_ops>();
    return ops && ops->read_object ? result<value>{value{ops->read_object(data_)}}
                                   : result<value>{error::unsupported};
  }
  if (kind() != type_kind::scalar)
    return error::unsupported;
  if (data_ && access() && access()->template get<scalar_ops>()->read) {
    auto output = access()->template get<scalar_ops>()->read(data_);
    if (output.kind() != scalar_kind() || !valid_scalar(output))
      return error::internal;
    return output;
  }
  return read_property(registry(), parent_type_, property_, parent_);
}
template <bool Mutable>
error basic_instance<Mutable>::assign(const value& input) const
  requires(Mutable)
{
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() == type_kind::variant && type_id() == runtime_type_of<value>() && mutable_data()) {
    *static_cast<value*>(mutable_data()) = input;
    return error::success;
  }
  if (kind() == type_kind::reference && access()) {
    const auto* ops = access()->template get<reference_ops>();
    if (!mutable_data() || !ops || !ops->write_object)
      return error::unsupported;
    const auto* object = std::get_if<ref<meta::object>>(&input.payload);
    return object ? ops->write_object(mutable_data(), *object) : error::invalid_argument;
  }
  if (kind() != type_kind::scalar)
    return error::unsupported;
  if (mutable_data() && access() && access()->template get<scalar_ops>()->write) {
    if (input.kind() != scalar_kind() || !valid_scalar(input))
      return error::invalid_argument;
    return access()->template get<scalar_ops>()->write(mutable_data(), input);
  }
  return write_property(registry(), parent_type_, property_, mutable_parent(), input);
}
template <bool Mutable>
error basic_instance<Mutable>::assign(const const_instance& source) const
  requires(Mutable)
{
  if (kind() == type_kind::invalid || source.kind() == type_kind::invalid)
    return error::invalid_state;
  if (requires_replacement()) {
    if (type_id() != source.type_id())
      return error::invalid_argument;
    auto binding = find_object_property(registry(), parent_type_, property_);
    if (!binding)
      return binding.error();
    return binding->object_setter(mutable_parent(), source.data_);
  }
  if ((kind() != type_kind::object && kind() != type_kind::reference) || source.kind() != kind())
    return error::unsupported;
  if (type_id() != source.type_id())
    return error::invalid_argument;
  if (!access() || !access()->copy_assign)
    return error::unsupported;
  if (mutable_data() != source.data_)
    access()->copy_assign(mutable_data(), source.data_);
  return error::success;
}
template <bool Mutable>
error basic_instance<Mutable>::assign_object(engine::type_id id, void* source) const
  requires(Mutable)
{
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (requires_replacement()) {
    if (!source || id != type_id())
      return error::invalid_argument;
    auto binding = find_object_property(registry(), parent_type_, property_);
    if (!binding)
      return binding.error();
    return binding->object_setter(mutable_parent(), source);
  }
  if (kind() != type_kind::object)
    return error::unsupported;
  if (!source || id != type_id())
    return error::invalid_argument;
  if (source == mutable_data())
    return error::success;
  if (access() && access()->move_assign) {
    access()->move_assign(mutable_data(), source);
    return error::success;
  }
  if (!mutable_parent())
    return error::unsupported;
  auto binding = find_object_property(registry(), parent_type_, property_);
  if (!binding)
    return binding.error();
  if (!binding->object_assign)
    return error::unsupported;
  binding->object_assign(mutable_parent(), source);
  return error::success;
}

template <bool Mutable>
error basic_instance<Mutable>::replace(
    const std::function<error(const instance&)>& initialize) const
  requires(Mutable)
{
  const auto& target = *this;
  if (!requires_replacement() || !initialize)
    return error::invalid_argument;
  auto binding = find_object_property(target.registry(), target.parent_type_, target.property_);
  if (!binding)
    return binding.error();
  auto storage = binding->object_construct();
  if (!storage)
    return error::out_of_memory;
  auto temporary = instance::from_access(target.registry(), *binding->access, storage.get());
  if (!temporary)
    return temporary.error();
  auto status = initialize(*temporary);
  return status.failed() ? status : binding->object_setter(target.mutable_parent(), storage.get());
}
template <bool Mutable>
result<basic_sequence_view<Mutable>> basic_instance<Mutable>::as_sequence() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::sequence || !data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!writable_)
      return error::unsupported;
  return basic_sequence_view<Mutable>{*this};
}
template <bool Mutable>
result<basic_associative_view<Mutable>> basic_instance<Mutable>::as_associative() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::associative || !data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!writable_)
      return error::unsupported;
  return basic_associative_view<Mutable>{*this};
}
template <bool Mutable>
result<basic_optional_view<Mutable>> basic_instance<Mutable>::as_optional() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::optional || !data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!writable_)
      return error::unsupported;
  return basic_optional_view<Mutable>{*this};
}
template <bool Mutable>
result<basic_reference_view<Mutable>> basic_instance<Mutable>::as_reference() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::reference || !data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!writable_)
      return error::unsupported;
  return basic_reference_view<Mutable>{*this};
}
template <bool Mutable>
result<basic_variant_view<Mutable>> basic_instance<Mutable>::as_variant() const noexcept {
  if (kind() == type_kind::invalid)
    return error::invalid_state;
  if (kind() != type_kind::variant || !data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!writable_)
      return error::unsupported;
  return basic_variant_view<Mutable>{*this};
}
template class basic_instance<false>;
template class basic_instance<true>;
result<const_instance> property::get(const const_instance& object) const {
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
result<instance> property::get(const instance& object) const {
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
