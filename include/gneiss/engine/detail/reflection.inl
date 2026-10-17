// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_REFLECTION_INL_
#define GNEISS_DETAIL_REFLECTION_INL_

#include <gneiss/engine/reflection.hpp>

#include <limits>
#include <new>
#include <stdexcept>

namespace gneiss {

namespace detail {
inline field_info reflection_field(const gneiss_field_info& value) noexcept {
  return {
      .id = field_id{value.id},
      .value_type_id = from_native(value.value_type_id),
      .flags = static_cast<field_flags>(value.flags),
      .name = value.name == nullptr ? std::string_view{}
                                    : std::string_view{value.name, value.name_length},
      .kind = static_cast<property_kind>(value.property_kind),
      .capabilities = static_cast<property_capabilities>(value.property_capabilities),
  };
}
inline result reflection_type(const gneiss_type_info& value, type_info& output) noexcept {
  try {
    type_info candidate;
    candidate.id = from_native(value.id);
    candidate.schema_version = value.schema_version;
    candidate.name = value.name == nullptr ? std::string_view{}
                                           : std::string_view{value.name, value.name_length};
    candidate.fields.reserve(value.field_count);
    for (std::uint32_t i = 0; i < value.field_count; ++i) {
      candidate.fields.push_back(reflection_field(value.fields[i]));
    }
    output = std::move(candidate);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}

inline gneiss_property_accessor_desc reflection_accessor(property_binding& binding) noexcept {
  gneiss_property_accessor_desc native = GNEISS_PROPERTY_ACCESSOR_DESC_INIT;
  native.kind = static_cast<std::uint32_t>(binding.callbacks.kind);
  native.user_data = &binding;
  if (binding.callbacks.getter != nullptr) {
    native.getter = [](void* data, gneiss_property_target target,
                       gneiss_property_value* output) noexcept -> gneiss_result {
      if (data == nullptr || output == nullptr) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto& cb = static_cast<property_binding*>(data)->callbacks;
      property_value value;
      const auto status = cb.getter(cb.user_data, from_native(target), value);
      if (status.failed()) {
        return status.native();
      }
      // 成功 getter 提供不可表示的值属于适配器错误，保持 C 属性边界的错误语义。
      return to_native(value, *output).ok() ? GNEISS_SUCCESS : GNEISS_ERROR_INTERNAL;
    };
  }
  if (binding.callbacks.setter != nullptr) {
    native.setter = [](void* data, gneiss_property_target target,
                       const gneiss_property_value* input) noexcept -> gneiss_result {
      if (data == nullptr || input == nullptr) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto& cb = static_cast<property_binding*>(data)->callbacks;
      property_value value;
      const auto status = from_native(*input, value);
      return status.failed() ? status.native()
                             : cb.setter(cb.user_data, from_native(target), value).native();
    };
  }
  return native;
}
} // namespace detail

namespace sdk {

inline result type_registry::create(type_registry& output) noexcept {
  gneiss_type_registry handle = GNEISS_NULL_TYPE_REGISTRY;
  const auto status = from_native(gneiss_type_registry_create(&handle));
  if (status.ok()) {
    type_registry candidate;
    candidate.handle_ = handle;
    const auto closed = output.reset();
    if (closed.failed()) {
      return closed;
    }
    output = std::move(candidate);
  }
  return status;
}

inline result type_registry::register_type(const type_desc& desc) const noexcept {
  constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
  if (desc.name.size() > limit || desc.fields.size() > limit) {
    return result::invalid_argument;
  }
  for (const auto& field : desc.fields) {
    if (field.name.size() > limit) {
      return result::invalid_argument;
    }
  }
  try {
    std::vector<gneiss_field_desc> fields;
    fields.reserve(desc.fields.size());
    for (const auto& field : desc.fields) {
      fields.push_back({
          .struct_size = sizeof(gneiss_field_desc),
          .id = field.id.get(),
          .value_type_id = to_native(field.value_type_id),
          .flags = static_cast<std::uint32_t>(field.flags),
          .name = field.name.data(),
          .name_length = static_cast<std::uint32_t>(field.name.size()),
      });
    }
    const gneiss_type_desc native{
        .struct_size = sizeof(gneiss_type_desc),
        .id = to_native(desc.id),
        .schema_version = desc.schema_version,
        .name = desc.name.data(),
        .name_length = static_cast<std::uint32_t>(desc.name.size()),
        .fields = fields.data(),
        .field_count = static_cast<std::uint32_t>(fields.size()),
    };
    return register_type_native(native);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}

inline result type_registry::bind_property(type_id type, field_id field,
                                           const property_accessor_desc& desc) noexcept {
  for (const auto& binding : bindings_) {
    if (binding->type == type && binding->field == field && binding->callbacks == desc) {
      const auto native = gneiss::detail::reflection_accessor(*binding);
      return bind_property_native(to_native(type), field.get(), native);
    }
  }
  try {
    auto candidate = std::make_unique<gneiss::detail::property_binding>(
        gneiss::detail::property_binding{.type = type, .field = field, .callbacks = desc});
    bindings_.reserve(bindings_.size() + 1);
    const auto native = gneiss::detail::reflection_accessor(*candidate);
    const auto status = bind_property_native(to_native(type), field.get(), native);
    if (status.ok()) {
      bindings_.push_back(std::move(candidate));
    }
    return status;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}

inline result type_registry::type_at(std::uint32_t index, type_info& output) const noexcept {
  gneiss_type_info value = GNEISS_TYPE_INFO_INIT;
  const auto status = type_at_native(index, value);
  return status.failed() ? status : gneiss::detail::reflection_type(value, output);
}

inline result type_registry::find_type(type_id id, type_info& output) const noexcept {
  gneiss_type_info value = GNEISS_TYPE_INFO_INIT;
  const auto status = find_type_native(to_native(id), value);
  return status.failed() ? status : gneiss::detail::reflection_type(value, output);
}

inline result type_registry::find_field(type_id type, field_id field,
                                        field_info& output) const noexcept {
  gneiss_field_info value = GNEISS_FIELD_INFO_INIT;
  const auto status = find_field_native(to_native(type), field.get(), value);
  if (status.ok()) {
    output = gneiss::detail::reflection_field(value);
  }
  return status;
}

inline result type_registry::get_property(type_id type, field_id field, property_target target,
                                          property_value& output) const noexcept {
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  const auto status = get_property_native(to_native(type), field.get(), to_native(target), value);
  return status.failed() ? status : from_native(value, output);
}

inline result type_registry::set_property(type_id type, field_id field, property_target target,
                                          const property_value& value) const noexcept {
  gneiss_property_value native = GNEISS_PROPERTY_VALUE_INIT;
  const auto status = to_native(value, native);
  return status.failed()
             ? status
             : set_property_native(to_native(type), field.get(), to_native(target), native);
}

inline result type_registry::register_type_native(const gneiss_type_desc& desc) const noexcept {
  return from_native(gneiss_type_registry_register(handle_, &desc));
}

inline result
type_registry::bind_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                    const gneiss_property_accessor_desc& desc) const noexcept {
  return from_native(gneiss_type_registry_bind_property(handle_, type_id, field_id, &desc));
}

inline result type_registry::freeze() const noexcept {
  return from_native(gneiss_type_registry_freeze(handle_));
}

inline result type_registry::is_frozen(bool& output) const noexcept {
  std::uint8_t value{};
  const auto status = from_native(gneiss_type_registry_is_frozen(handle_, &value));
  if (status.ok()) {
    output = value != 0U;
  }
  return status;
}

inline result type_registry::type_at_native(std::uint32_t index,
                                            gneiss_type_info& output) const noexcept {
  output = GNEISS_TYPE_INFO_INIT;
  return from_native(gneiss_type_registry_type_at(handle_, index, &output));
}

inline result type_registry::type_count(std::uint32_t& output) const noexcept {
  return from_native(gneiss_type_registry_type_count(handle_, &output));
}

inline result type_registry::find_type_native(gneiss_type_id id,
                                              gneiss_type_info& output) const noexcept {
  output = GNEISS_TYPE_INFO_INIT;
  return from_native(gneiss_type_registry_find_type(handle_, id, &output));
}

inline result type_registry::find_field_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                               gneiss_field_info& output) const noexcept {
  output = GNEISS_FIELD_INFO_INIT;
  return from_native(gneiss_type_registry_find_field(handle_, type_id, field_id, &output));
}

inline result type_registry::get_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                                 gneiss_property_target target,
                                                 gneiss_property_value& output) const noexcept {
  return from_native(
      gneiss_type_registry_get_property(handle_, type_id, field_id, target, &output));
}

inline result
type_registry::set_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                   gneiss_property_target target,
                                   const gneiss_property_value& value) const noexcept {
  return from_native(gneiss_type_registry_set_property(handle_, type_id, field_id, target, &value));
}

inline result type_registry::reset() noexcept {
  if (handle_ == GNEISS_NULL_TYPE_REGISTRY) {
    return result::success;
  }
  const auto status = from_native(gneiss_type_registry_destroy(handle_));
  if (status.failed() && status != result::invalid_handle) {
    return status;
  }
  handle_ = GNEISS_NULL_TYPE_REGISTRY;
  bindings_.clear();
  return result::success;
}

inline void type_registry::reset_or_terminate() noexcept {
  if (reset().failed()) {
    std::terminate();
  }
}

inline result released_type_registry::reset() noexcept { return owner_.reset(); }

inline released_type_registry type_registry::release() noexcept {
  return released_type_registry{std::move(*this)};
}
inline result type_registry::adopt(released_type_registry&& source,
                                   type_registry& output) noexcept {
  const auto status = output.reset();
  if (status.failed()) {
    return status;
  }
  output = std::move(source.owner_);
  return result::success;
}

} // namespace sdk
} // namespace gneiss

#endif
