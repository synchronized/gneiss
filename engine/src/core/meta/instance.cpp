// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <algorithm>
#include <gneiss/engine/assert.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/capabilities/representation.hpp>
#include <gneiss/engine/core/meta/capabilities/sequence.hpp>
#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta {
cap::representation::representation(meta::type target)
    : type_(target.access_ && target.access_->representation ? std::move(target) : meta::type{}) {}
error cap::representation::decode(const std::function<error(const instance&)>& read,
                                  const std::function<error(const instance&)>& consume) const {
  GNEISS_ASSERT(*this && read && consume, "表示转换要求有效能力与回调");
  return type_.access_->representation->decode(
      [&](void* p, const type_ops& ops) {
        auto view = instance::from_access(type_.registry_, ops, p);
        return view ? read(*view) : view.error();
      },
      [&](void* p, const type_ops& ops) {
        auto view = instance::from_access(type_.registry_, ops, p);
        return view ? consume(*view) : view.error();
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
    return view ? consume(*view) : view.error();
  };
  if (source.data_)
    return ops->encode(source.data_, visit);
  if (!ops->encode_value)
    return error::unsupported;
  auto value = source.read_value();
  return value ? ops->encode_value(*value, visit) : value.error();
}
bool cap::sequence::is_fixed_size() const noexcept {
  GNEISS_ASSERT(*this, "序列类型查询要求有效视图");
  return type_.access_->get<sequence_ops>()->fixed_size;
}
bool cap::sequence::has_identity() const noexcept {
  GNEISS_ASSERT(*this, "序列类型查询要求有效视图");
  return type_.access_->get<sequence_ops>()->identity != nullptr;
}
template <type_kind Kind, template <bool> class View>
meta::type cap::container<Kind, View>::element_type_impl() const {
  GNEISS_ASSERT(*this, "元素类型查询要求有效能力");
  return detail::resolve_type(type_.registry_, type_.access_->element());
}
template GNEISS_API meta::type
cap::container<type_kind::sequence, basic_sequence_view>::element_type_impl() const;
template GNEISS_API meta::type
cap::container<type_kind::associative, basic_associative_view>::element_type_impl() const;
template GNEISS_API meta::type
cap::container<type_kind::optional, basic_optional_view>::element_type_impl() const;
template GNEISS_API meta::type
cap::container<type_kind::reference, basic_reference_view>::element_type_impl() const;

template <bool Mutable>
result<basic_instance<Mutable>>
basic_instance<Mutable>::from_object(registry_ref registry, gneiss::type_id id, pointer object) {
  if (!object)
    return error::invalid_argument;
  auto frozen = is_frozen(registry);
  if (!frozen)
    return frozen.error();
  if (!*frozen)
    return error::not_ready;
  return from_object(find_type(registry, id), object);
}
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
result<basic_instance<Mutable>>
basic_instance<Mutable>::from_access(registry_ref registry, const type_ops& access, pointer data) {
  GNEISS_ASSERT(data, "实例适配器必须提供有效存储");
  basic_instance output;
  output.type_ = detail::resolve_type(registry, access);
  if (!output.type_)
    return error::invalid_state;
  output.data_ = data;
  return output;
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_object_view<Mutable>::member(std::string_view name) const {
  if (target_.kind() == type_kind::invalid)
    return error::invalid_state;
  if (target_.kind() != type_kind::object)
    return error::unsupported;
  const auto field =
      target_.type_.template capability<gneiss::meta::cap::object>().find_property(name);
  return field ? member(field.id()) : result<basic_instance<Mutable>>{error::not_found};
}
template <bool Mutable>
result<basic_instance<Mutable>> basic_object_view<Mutable>::member(property_id id) const {
  if (target_.kind() == type_kind::invalid)
    return error::invalid_state;
  if (target_.kind() != type_kind::object || !target_.data_)
    return error::unsupported;
  if constexpr (Mutable)
    if (!target_.writable_)
      return error::unsupported;
  const auto field =
      target_.type_.template capability<gneiss::meta::cap::object>().find_property(id);
  if (!field)
    return error::not_found;
  if (field.declaring_type_id() != target_.type_id()) {
    auto ops = find_object_ops(target_.registry(), target_.type_id());
    if (!ops || !ops->upcast)
      return ops ? error::invalid_state : ops.error();
    auto base = basic_instance<Mutable>::from_object(target_.registry(), ops->base_type,
                                                     ops->upcast(const_cast<void*>(target_.data_)));
    if (!base)
      return base.error();
    auto object = base->template capability<cap::object>();
    return object ? object->member(id) : result<basic_instance<Mutable>>{object.error()};
  }
  if constexpr (Mutable)
    if (!has_flags(field.capabilities(), property_capabilities::writable))
      return error::unsupported;
  basic_instance<Mutable> output;
  output.type_ = field.type();
  output.parent_ = target_.data_;
  output.binding_ = field.binding();
  if (output.kind() == type_kind::scalar) {
    if (output.scalar_kind() == value_kind::invalid)
      return error::unsupported;
    return output;
  }
  const auto* binding = output.binding_;
  if (!binding->object_getter)
    return error::unsupported;
  output.data_ = binding->object_getter(target_.data_);
  if constexpr (Mutable) {
    if (binding->object_setter)
      output.writable_ = false;
    else {
      if (!binding->object_mutator)
        return error::unsupported;
      output.data_ = binding->object_mutator(target_.mutable_data());
    }
  }
  GNEISS_ASSERT(output.data_, "已绑定成员访问器必须返回有效成员地址");
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
  GNEISS_ASSERT(kind() != type_kind::invalid, "读取值要求有效的借用实例");
  if (kind() == type_kind::variant && type_id() == runtime_type_of<value>() && data_)
    return *static_cast<const value*>(data_);
  if (kind() == type_kind::reference && access()) {
    const auto* ops = access()->template get<reference_ops>();
    return ops && ops->read_object ? result<value>{value{ops->read_object(data_)}}
                                   : result<value>{error::unsupported};
  }
  if (data_ && access()) {
    if (const auto* ops = access()->template get<value_ops>()) {
      auto output = ops->read(data_);
      if (kind() == type_kind::scalar) {
        GNEISS_ASSERT(output.kind() == scalar_kind(), "标量读取必须遵守登记的值类型");
        if (!valid_scalar(output))
          return error::invalid_argument;
      }
      return output;
    }
  }
  if (kind() != type_kind::scalar || !binding_ || !binding_->getter)
    return error::unsupported;
  value output;
  auto status = binding_->getter(binding_->user_data, parent_, output);
  if (status.failed())
    return status;
  GNEISS_ASSERT(output.kind() == scalar_kind(), "getter 成功时必须返回登记的值类型");
  if (!valid_scalar(output))
    return error::invalid_argument;
  return output;
}
template <bool Mutable>
error basic_instance<Mutable>::assign(const value& input) const
  requires(Mutable)
{
  GNEISS_ASSERT(kind() != type_kind::invalid, "写入要求有效的目标实例");
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
  if (access()) {
    if (const auto* ops = access()->template get<value_ops>()) {
      if (kind() == type_kind::scalar && (input.kind() != scalar_kind() || !valid_scalar(input)))
        return error::invalid_argument;
      if (requires_replacement())
        return replace([input_ptr = &input](const instance& candidate) {
          return candidate.assign(*input_ptr);
        });
      if (mutable_data())
        return ops->write(mutable_data(), input);
    }
  }
  if (kind() != type_kind::scalar || !binding_ || !binding_->setter || !writable_)
    return error::unsupported;
  if (input.kind() != scalar_kind() || !valid_scalar(input))
    return error::invalid_argument;
  return binding_->setter(binding_->user_data, mutable_parent(), input);
}
template <bool Mutable>
error basic_instance<Mutable>::assign(const const_instance& source) const
  requires(Mutable)
{
  GNEISS_ASSERT(kind() != type_kind::invalid, "写入要求有效的目标实例");
  GNEISS_ASSERT(source.kind() != type_kind::invalid, "复制要求有效的来源实例");
  if (requires_replacement()) {
    if (type_id() != source.type_id())
      return error::invalid_argument;
    const auto* binding = binding_;
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
error basic_instance<Mutable>::assign_object(gneiss::type_id id, void* source) const
  requires(Mutable)
{
  GNEISS_ASSERT(kind() != type_kind::invalid, "写入要求有效的目标实例");
  if (requires_replacement()) {
    if (!source || id != type_id())
      return error::invalid_argument;
    const auto* binding = binding_;
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
  const auto* binding = binding_;
  if (!binding)
    return error::unsupported;
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
  GNEISS_ASSERT(kind() != type_kind::invalid, "替换要求有效的目标实例");
  const auto& target = *this;
  if (!requires_replacement() || !initialize)
    return error::invalid_argument;
  const auto* binding = target.binding_;
  auto storage = binding->object_construct();
  GNEISS_ASSERT(storage, "属性构造器必须返回有效候选对象，分配失败通过异常传播");
  auto temporary = instance::from_access(target.registry(), *binding->access, storage.get());
  if (!temporary)
    return temporary.error();
  auto status = initialize(*temporary);
  return status.failed() ? status : binding->object_setter(target.mutable_parent(), storage.get());
}
template <type_kind Kind, template <bool> class View>
template <bool Mutable>
result<View<Mutable>>
cap::container<Kind, View>::bind(const basic_instance<Mutable>& source) const {
  if (source.kind() == type_kind::invalid)
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
template GNEISS_API result<basic_sequence_view<false>>
cap::container<type_kind::sequence, basic_sequence_view>::bind(const basic_instance<false>&) const;
template GNEISS_API result<basic_sequence_view<true>>
cap::container<type_kind::sequence, basic_sequence_view>::bind(const basic_instance<true>&) const;
template <bool Mutable>
result<basic_associative_view<Mutable>> basic_instance<Mutable>::as_associative() const noexcept {
  return capability<cap::associative>();
}
template GNEISS_API result<basic_associative_view<false>>
cap::container<type_kind::associative, basic_associative_view>::bind(
    const basic_instance<false>&) const;
template GNEISS_API result<basic_associative_view<true>>
cap::container<type_kind::associative, basic_associative_view>::bind(
    const basic_instance<true>&) const;
template <bool Mutable>
result<basic_optional_view<Mutable>> basic_instance<Mutable>::as_optional() const noexcept {
  return capability<cap::optional>();
}
template GNEISS_API result<basic_optional_view<false>>
cap::container<type_kind::optional, basic_optional_view>::bind(const basic_instance<false>&) const;
template GNEISS_API result<basic_optional_view<true>>
cap::container<type_kind::optional, basic_optional_view>::bind(const basic_instance<true>&) const;
template <bool Mutable>
result<basic_reference_view<Mutable>> basic_instance<Mutable>::as_reference() const noexcept {
  return capability<cap::reference>();
}
template GNEISS_API result<basic_reference_view<false>>
cap::container<type_kind::reference, basic_reference_view>::bind(
    const basic_instance<false>&) const;
template GNEISS_API result<basic_reference_view<true>>
cap::container<type_kind::reference, basic_reference_view>::bind(const basic_instance<true>&) const;
template <bool Mutable>
result<basic_variant_view<Mutable>> basic_instance<Mutable>::as_variant() const noexcept {
  return capability<cap::variant>();
}
template GNEISS_API result<basic_variant_view<false>>
cap::container<type_kind::variant, basic_variant_view>::bind(const basic_instance<false>&) const;
template GNEISS_API result<basic_variant_view<true>>
cap::container<type_kind::variant, basic_variant_view>::bind(const basic_instance<true>&) const;

template <bool Mutable> property_range basic_object_view<Mutable>::properties() const noexcept {
  return target_.type().template capability<cap::object>().properties();
}
template <bool Mutable>
result<basic_object_view<Mutable>> cap::object::bind(const basic_instance<Mutable>& source) const {
  if (source.kind() == type_kind::invalid)
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
template GNEISS_API result<basic_object_view<false>>
cap::object::bind(const basic_instance<false>&) const;
template GNEISS_API result<basic_object_view<true>>
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
  if (info.declaring_type_id() != declaring_type_id() || info.declaring_type() != declaring_type())
    return error::invalid_argument;
  auto bound = object.capability<cap::object>();
  return bound ? bound->member(id()) : result<const_instance>{bound.error()};
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
  if (info.declaring_type_id() != declaring_type_id() || info.declaring_type() != declaring_type())
    return error::invalid_argument;
  auto bound = object.capability<cap::object>();
  return bound ? bound->member(id()) : result<instance>{bound.error()};
}
}
