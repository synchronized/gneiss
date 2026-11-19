// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "instance_access.hpp"
#include "temporary_value.hpp"
#include <algorithm>
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/capabilities/representation.hpp>
#include <gneiss/engine/core/meta/capabilities/sequence.hpp>
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/meta/resolve.hpp>
#include <gneiss/engine/core/meta/type_definition.hpp>

namespace gneiss::meta {
cap::representation::representation(meta::type target)
    : type_(target.access_ && target.access_->representation ? std::move(target) : meta::type{}) {}
error cap::representation::decode(const std::function<error(const instance&)>& read,
                                  const std::function<error(const instance&)>& consume) const {
  GNEISS_ASSERT(*this && read && consume, "表示转换要求有效能力与回调");
  return type_.access_->representation->decode(
      [&](void* p, const type_ops& ops) {
        auto view = instance::from_access(type_.registry_, ops, p);
        return read(view);
      },
      [&](void* p, const type_ops& ops) {
        auto view = instance::from_access(type_.registry_, ops, p);
        return consume(view);
      });
}
error cap::representation::encode(
    const const_instance& source,
    const std::function<error(const const_instance&)>& consume) const {
  GNEISS_ASSERT(*this && consume, "表示读取要求有效能力与回调");
  if (source.type() != type_)
    return error::invalid_argument;
  const auto* ops = type_.access_->representation;
  auto visit = [&](const void* p, const type_ops& representation) {
    auto view = const_instance::from_access(type_.registry_, representation, p);
    return consume(view);
  };
  return detail::instance_access::read(
      source, [&](const const_instance& concrete) { return ops->encode(concrete.data_, visit); });
}
bool cap::sequence::is_fixed_size() const noexcept {
  GNEISS_ASSERT(*this, "序列类型查询要求有效视图");
  return type_.access_->get<sequence_ops>()->fixed_size;
}
bool cap::sequence::has_identity() const noexcept {
  GNEISS_ASSERT(*this, "序列类型查询要求有效视图");
  return type_.access_->get<sequence_ops>()->identity != nullptr;
}
bool cap::sequence::is_resizable() const noexcept {
  GNEISS_ASSERT(*this, "序列类型查询要求有效能力");
  return type_.access_->get<sequence_ops>()->resize != nullptr;
}
bool cap::sequence::supports_replacement() const noexcept {
  GNEISS_ASSERT(*this, "序列类型查询要求有效能力");
  return type_.access_->get<sequence_ops>()->replace != nullptr;
}
std::size_t cap::variant::alternative_count() const noexcept {
  GNEISS_ASSERT(*this, "分支查询要求有效能力");
  return type_.access_->get<variant_ops>()->alternatives.size();
}
std::span<const std::string_view> cap::variant::names() const noexcept {
  GNEISS_ASSERT(*this, "分支查询要求有效能力");
  return type_.access_->get<variant_ops>()->names;
}
result<meta::type> cap::variant::alternative(std::size_t index) const {
  if (index >= alternative_count())
    return error::invalid_argument;
  return detail::resolve_type(type_.registry_,
                              *type_.access_->get<variant_ops>()->alternatives[index]);
}

template <type_kind Kind, template <bool> class View>
meta::type cap::container<Kind, View>::element_type_impl() const {
  GNEISS_ASSERT(*this, "元素类型查询要求有效能力");
  return detail::resolve_type(type_.registry_, type_.access_->element());
}
template GNEISS_META_API meta::type
cap::container<type_kind::sequence, basic_sequence_view>::element_type_impl() const;
template GNEISS_META_API meta::type
cap::container<type_kind::associative, basic_associative_view>::element_type_impl() const;
template GNEISS_META_API meta::type
cap::container<type_kind::optional, basic_optional_view>::element_type_impl() const;
template GNEISS_META_API meta::type
cap::container<type_kind::reference, basic_reference_view>::element_type_impl() const;

template <bool Mutable>
result<basic_instance<Mutable>> basic_instance<Mutable>::from_object(meta::type description,
                                                                     pointer object) {
  if (!object)
    return error::invalid_argument;
  if (!description || description.kind() != type_kind::object)
    return error::not_found;
  basic_instance output;
  output.type_ = std::move(description);
  output.data_ = object;
  return output;
}

template <bool Mutable>
basic_instance<Mutable> basic_instance<Mutable>::from_access(registry_ref registry,
                                                             const type_ops& access, pointer data) {
  GNEISS_ASSERT(data, "实例适配器必须提供有效存储");
  basic_instance output;
  output.type_ = detail::resolve_type(registry, access);
  GNEISS_ASSERT(output.type_, "静态类型能力必须提供有效类型");
  output.data_ = data;
  return output;
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_object_view<Mutable>::cast(meta::type type) const {
  auto address = target_.type_.template capability<cap::object>().cast_address(type, target_.data_);
  if (!address)
    return address.error();
  if constexpr (Mutable)
    return instance::from_object(std::move(type), const_cast<void*>(*address));
  else
    return const_instance::from_object(std::move(type), *address);
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_object_view<Mutable>::member(std::string_view name) const {
  if (target_.type().kind() == type_kind::invalid)
    return error::invalid_state;
  if (target_.type().kind() != type_kind::object)
    return error::unsupported;
  const auto field =
      target_.type_.template capability<gneiss::meta::cap::object>().find_property(name);
  return field ? bind_field(field) : result<basic_instance<Mutable>>{error::not_found};
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_object_view<Mutable>::member(property_id id) const {
  if (target_.type().kind() == type_kind::invalid)
    return error::invalid_state;
  if (target_.type().kind() != type_kind::object || !target_.data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!target_.writable_)
      return error::unsupported;
  const auto field =
      target_.type_.template capability<gneiss::meta::cap::object>().find_property(id);
  if (!field)
    return error::not_found;
  return bind_field(field);
}
template <bool Mutable>
result<basic_instance<Mutable>>
basic_object_view<Mutable>::bind_field(const property& field) const {
  if (field.declaring_type() != target_.type()) {
    auto base_type = target_.type_.template capability<cap::object>().base_type();
    auto base = cast(base_type);
    if (!base)
      return base.error();
    auto object = base->template capability<cap::object>();
    return object ? object->bind_field(field) : result<basic_instance<Mutable>>{object.error()};
  }
  if constexpr (Mutable)
    if (!has_flags(field.capabilities(), property_capabilities::writable))
      return error::unsupported;
  basic_instance<Mutable> output;
  output.type_ = field.type();
  output.parent_ = target_.data_;
  output.binding_ = field.binding();
  const auto* binding = output.binding_;
  if (!binding || (!binding->const_borrow && !binding->read_construct && !binding->write))
    return error::unsupported;
  if (binding->const_borrow) {
    output.data_ = binding->const_borrow(binding->state, target_.data_);
    GNEISS_ASSERT(output.data_, "借用访问器必须返回有效地址");
  }
  if constexpr (Mutable) {
    if (binding->write)
      output.writable_ = false;
    else {
      if (!binding->borrow)
        return error::unsupported;
      output.data_ = binding->borrow(binding->state, target_.mutable_data());
      GNEISS_ASSERT(output.data_, "可写借用访问器必须返回有效地址");
    }
  }
  return output;
}
template <bool Mutable>
result<typename basic_instance<Mutable>::pointer>
basic_instance<Mutable>::object_data() const noexcept {
  if (type().kind() == type_kind::invalid)
    return error::invalid_state;
  if (type().kind() != type_kind::object)
    return error::unsupported;
  if constexpr (Mutable) {
    if (!mutable_data())
      return error::unsupported;
    return mutable_data();
  } else {
    if (!data_)
      return error::unsupported;
    return data_;
  }
}
error detail::instance_access::read(const const_instance& source,
                                    const std::function<error(const const_instance&)>& consume) {
  GNEISS_ASSERT(source.type_ && consume, "值访问要求有效实例与回调");
  if (source.data_)
    return consume(source);
  if (!source.binding_ || !source.binding_->read_construct || !source.access())
    return error::unsupported;
  temporary_value storage{*source.access()};
  auto status = storage.construct([&](void* destination) {
    return source.binding_->read_construct(source.binding_->state, source.parent_, destination);
  });
  if (status.failed())
    return status;
  auto temporary = const_instance::from_access(source.registry(), *source.access(), storage.data());
  return consume(temporary);
}

template <bool Mutable> result<value> basic_instance<Mutable>::read_value() const {
  GNEISS_ASSERT(type().kind() != type_kind::invalid, "读取值要求有效的借用实例");
  if (!data_) {
    result<value> output = error::unsupported;
    auto status = detail::instance_access::read(as_const(), [&](const const_instance& source) {
      output = source.read_value();
      return output ? error::success : output.error();
    });
    return status.failed() ? result<value>{status} : output;
  }
  if (type().kind() == type_kind::variant && type().id() == runtime_type_of<value>() && data_)
    return *static_cast<const value*>(data_);
  if (type().kind() == type_kind::reference && access()) {
    const auto* ops = access()->template get<reference_ops>();
    return ops && ops->read_object ? result<value>{value{ops->read_object(data_)}}
                                   : result<value>{error::unsupported};
  }
  if (data_ && access()) {
    if (const auto* ops = access()->template get<value_ops>()) {
      auto output = ops->read(data_);
      if (type().kind() == type_kind::scalar) {
        GNEISS_ASSERT(output.kind() == type().scalar_kind(), "标量读取必须遵守登记的值类型");
        if (!valid_scalar(output))
          return error::invalid_argument;
      }
      return output;
    }
  }
  return error::unsupported;
}
template <bool Mutable>
error basic_instance<Mutable>::assign(const value& input) const
  requires(Mutable)
{
  GNEISS_ASSERT(type().kind() != type_kind::invalid, "写入要求有效的目标实例");
  if (detail::instance_access::constrained(*this))
    return detail::instance_access::replace(*this, [input_ptr = &input](const instance& candidate) {
      return candidate.assign(*input_ptr);
    });

  if (type().kind() == type_kind::variant && type().id() == runtime_type_of<value>() &&
      mutable_data()) {
    *static_cast<value*>(mutable_data()) = input;
    return error::success;
  }
  if (type().kind() == type_kind::reference && access()) {
    const auto* ops = access()->template get<reference_ops>();
    if (!mutable_data() || !ops || !ops->write_object)
      return error::unsupported;
    const auto* object = std::get_if<ref<meta::object>>(&input.payload);
    return object ? ops->write_object(mutable_data(), *object) : error::invalid_argument;
  }
  if (access()) {
    if (const auto* ops = access()->template get<value_ops>()) {
      if (type().kind() == type_kind::scalar &&
          (input.kind() != type().scalar_kind() || !valid_scalar(input)))
        return error::invalid_argument;
      if (mutable_data())
        return ops->write(mutable_data(), input);
    }
  }
  return error::unsupported;
}
template <bool Mutable>
error basic_instance<Mutable>::assign(const const_instance& source) const
  requires(Mutable)
{
  GNEISS_ASSERT(type().kind() != type_kind::invalid, "写入要求有效的目标实例");
  GNEISS_ASSERT(source.type().kind() != type_kind::invalid, "复制要求有效的来源实例");
  if (type().id() != source.type().id())
    return error::invalid_argument;
  return detail::instance_access::read(source, [&](const const_instance& concrete) {
    if (detail::instance_access::constrained(*this))
      return binding_->write(binding_->state, mutable_parent(), concrete.data_);
    if (!mutable_data() || !access() || !access()->copy_assign)
      return error::unsupported;
    if (mutable_data() != concrete.data_)
      access()->copy_assign(mutable_data(), concrete.data_);
    return error::success;
  });
}
template <bool Mutable>
error basic_instance<Mutable>::move_from(const instance& source_view) const
  requires(Mutable)
{
  GNEISS_ASSERT(type().kind() != type_kind::invalid, "写入要求有效的目标实例");
  GNEISS_ASSERT(source_view.type(), "移动要求有效的来源实例");
  if (source_view.type().id() != type().id() || source_view.type().kind() != type().kind())
    return error::invalid_argument;
  auto* source = source_view.mutable_data();
  if (!source)
    return error::unsupported;
  if (detail::instance_access::constrained(*this)) {
    const auto* binding = binding_;
    return binding->write(binding->state, mutable_parent(), source);
  }
  if (type().kind() != type_kind::object)
    return error::unsupported;
  if (source == mutable_data())
    return error::success;
  if (access() && access()->move_assign) {
    access()->move_assign(mutable_data(), source);
    return error::success;
  }
  return error::unsupported;
}

error detail::instance_access::replace(const instance& target,
                                       const std::function<error(const instance&)>& initialize) {
  GNEISS_ASSERT(target.type_, "替换要求有效的目标实例");
  if (!constrained(target) || !initialize)
    return error::invalid_argument;
  temporary_value storage{*target.access()};
  auto status = storage.construct_default();
  if (status.failed())
    return status;
  auto temporary = instance::from_access(target.registry(), *target.access(), storage.data());
  status = initialize(temporary);
  return status.failed() ? status
                         : target.binding_->write(target.binding_->state, target.mutable_parent(),
                                                  storage.data());
}

error detail::instance_access::edit(const instance& target,
                                    const std::function<error(const instance&)>& modify) {
  GNEISS_ASSERT(target.type_ && modify, "属性编辑要求有效实例与回调");
  if (!constrained(target) || !target.access() || !target.access()->copy_construct)
    return error::unsupported;
  return read(target.as_const(), [&](const const_instance& current) {
    temporary_value storage{*target.access()};
    auto status = storage.construct([&](void* destination) {
      target.access()->copy_construct(destination, current.data_);
      return error::success;
    });
    if (status.failed())
      return status;
    auto candidate = instance::from_access(target.registry(), *target.access(), storage.data());
    status = modify(candidate);
    return status.failed() ? status
                           : target.binding_->write(target.binding_->state, target.mutable_parent(),
                                                    storage.data());
  });
}

template <type_kind Kind, template <bool> class View>
template <bool Mutable>
result<View<Mutable>>
cap::container<Kind, View>::bind(const basic_instance<Mutable>& source) const {
  if (source.type().kind() == type_kind::invalid)
    return error::invalid_state;
  if (!*this)
    return error::unsupported;
  if (source.type() != type_)
    return error::invalid_argument;
  if (!source.data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!source.writable_)
      return error::unsupported;
  return View<Mutable>{source};
}
template <bool Mutable>
result<basic_sequence_view<Mutable>> basic_instance<Mutable>::as_sequence() const noexcept {
  return capability<cap::sequence>();
}
template GNEISS_META_API result<basic_sequence_view<false>>
cap::container<type_kind::sequence, basic_sequence_view>::bind(const basic_instance<false>&) const;
template GNEISS_META_API result<basic_sequence_view<true>>
cap::container<type_kind::sequence, basic_sequence_view>::bind(const basic_instance<true>&) const;
template <bool Mutable>
result<basic_associative_view<Mutable>> basic_instance<Mutable>::as_associative() const noexcept {
  return capability<cap::associative>();
}
template GNEISS_META_API result<basic_associative_view<false>>
cap::container<type_kind::associative, basic_associative_view>::bind(
    const basic_instance<false>&) const;
template GNEISS_META_API result<basic_associative_view<true>>
cap::container<type_kind::associative, basic_associative_view>::bind(
    const basic_instance<true>&) const;
template <bool Mutable>
result<basic_optional_view<Mutable>> basic_instance<Mutable>::as_optional() const noexcept {
  return capability<cap::optional>();
}
template GNEISS_META_API result<basic_optional_view<false>>
cap::container<type_kind::optional, basic_optional_view>::bind(const basic_instance<false>&) const;
template GNEISS_META_API result<basic_optional_view<true>>
cap::container<type_kind::optional, basic_optional_view>::bind(const basic_instance<true>&) const;
template <bool Mutable>
result<basic_reference_view<Mutable>> basic_instance<Mutable>::as_reference() const noexcept {
  return capability<cap::reference>();
}
template GNEISS_META_API result<basic_reference_view<false>>
cap::container<type_kind::reference, basic_reference_view>::bind(
    const basic_instance<false>&) const;
template GNEISS_META_API result<basic_reference_view<true>>
cap::container<type_kind::reference, basic_reference_view>::bind(const basic_instance<true>&) const;
template <bool Mutable>
result<basic_variant_view<Mutable>> basic_instance<Mutable>::as_variant() const noexcept {
  return capability<cap::variant>();
}
template GNEISS_META_API result<basic_variant_view<false>>
cap::container<type_kind::variant, basic_variant_view>::bind(const basic_instance<false>&) const;
template GNEISS_META_API result<basic_variant_view<true>>
cap::container<type_kind::variant, basic_variant_view>::bind(const basic_instance<true>&) const;

template <bool Mutable> property_range basic_object_view<Mutable>::properties() const noexcept {
  return target_.type().template capability<cap::object>().properties();
}
template <bool Mutable>
result<basic_object_view<Mutable>> cap::object::bind(const basic_instance<Mutable>& source) const {
  if (source.type().kind() == type_kind::invalid)
    return error::invalid_state;
  if (!*this)
    return error::unsupported;
  if (source.type() != type_)
    return error::invalid_argument;
  if (!source.data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!source.writable_)
      return error::unsupported;
  return basic_object_view<Mutable>{source};
}
template GNEISS_META_API result<basic_object_view<false>>
cap::object::bind(const basic_instance<false>&) const;
template GNEISS_META_API result<basic_object_view<true>>
cap::object::bind(const basic_instance<true>&) const;
template class basic_object_view<false>;
template class basic_object_view<true>;

template class basic_instance<false>;
template class basic_instance<true>;
result<const_instance> property::get(const const_instance& object) const {
  if (!record_)
    return error::invalid_state;
  if (registry_ != object.registry())
    return error::invalid_argument;
  auto info = object.type().template capability<gneiss::meta::cap::object>().find_property(id());
  if (!info)
    return error::not_found;
  if (info.declaring_type() != declaring_type())
    return error::invalid_argument;
  auto bound = object.capability<cap::object>();
  return bound ? bound->bind_field(info) : result<const_instance>{bound.error()};
}
result<instance> property::get(const instance& object) const {
  if (!record_)
    return error::invalid_state;
  const auto& view = object.as_const();
  if (registry_ != view.registry())
    return error::invalid_argument;
  auto info = view.type().template capability<gneiss::meta::cap::object>().find_property(id());
  if (!info)
    return error::not_found;
  if (info.declaring_type() != declaring_type())
    return error::invalid_argument;
  auto bound = object.capability<cap::object>();
  return bound ? bound->bind_field(info) : result<instance>{bound.error()};
}
}
