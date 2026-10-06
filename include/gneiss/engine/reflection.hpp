// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_HPP_
#define GNEISS_REFLECTION_HPP_

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/reflection.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gneiss {

/** 稳定类型标识；全零无效，不拥有 Registry。 */
struct type_id {
  std::array<std::uint8_t, 16> bytes{};
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return std::ranges::any_of(bytes, [](auto value) { return value != 0; });
  }
  friend constexpr bool operator==(const type_id&, const type_id&) noexcept = default;
};
/** 类型内部的字段身份，零无效。 */
class field_id final {
public:
  constexpr field_id() noexcept = default;
  explicit constexpr field_id(std::uint32_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint32_t get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  friend constexpr bool operator==(field_id, field_id) noexcept = default;

private:
  std::uint32_t value_ = 0;
};
inline constexpr field_id null_field_id{};
// NOLINTNEXTLINE(performance-enum-size): 未知值需保留完整 ABI 数值后交给边界验证。
enum class property_kind : std::uint32_t {
  invalid = 0,
  boolean = 1,
  int64 = 2,
  uint64 = 3,
  float32 = 4,
  float64 = 5,
  string = 6,
  type_id = 7,
  vec3 = 8,
  quaternion = 9,
};
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class field_flags : std::uint32_t { none = 0, read_only = 1 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class property_capabilities : std::uint32_t { none = 0, readable = 1, writable = 2 };
template <typename T>
concept reflection_flags = std::same_as<T, field_flags> || std::same_as<T, property_capabilities>;
template <reflection_flags T> [[nodiscard]] constexpr T operator|(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
template <reflection_flags T> [[nodiscard]] constexpr T operator&(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
template <reflection_flags T> constexpr T& operator|=(T& left, T right) noexcept {
  return left = left | right;
}
template <reflection_flags T> constexpr T& operator&=(T& left, T right) noexcept {
  return left = left & right;
}
template <reflection_flags T> [[nodiscard]] constexpr bool has_flags(T value, T flags) noexcept {
  return (value & flags) == flags;
}
struct property_vec3 {
  friend constexpr bool operator==(const property_vec3&, const property_vec3&) noexcept = default;
  float x = 0;
  float y = 0;
  float z = 0;
};
struct property_quaternion {
  friend constexpr bool operator==(const property_quaternion&,
                                   const property_quaternion&) noexcept = default;
  float x = 0;
  float y = 0;
  float z = 0;
  float w = 1;
};
/** 属性文本的寿命由 getter 的适配器约定，包装不延长寿命。 */
using property_string = std::string_view;
using property_payload =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, property_string,
                 type_id, property_vec3, property_quaternion>;
struct property_value {
  property_payload payload;
  [[nodiscard]] property_kind kind() const noexcept {
    return payload.valueless_by_exception() ? property_kind::invalid
                                            : static_cast<property_kind>(payload.index());
  }
};
/** 不透明目标语义由访问器定义；不拥有 Context 或 Object。 */
struct property_target {
  std::uint64_t context = 0;
  std::uint64_t object = 0;
};
// 缺省成员值允许 designated 初始化省略字段，避免消费者缺省字段告警。
// NOLINTBEGIN(readability-redundant-member-init)
struct field_desc {
  field_id id{};
  type_id value_type_id{};
  field_flags flags = field_flags::none;
  std::string_view name{};
};
/** 输入文本/数组只需存活到注册返回；Registry 深拷贝。 */
struct type_desc {
  type_id id{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::span<const field_desc> fields{};
};
/** 名称由冻结 Registry 借出，有效期不超过 Registry。 */
struct field_info {
  field_id id{};
  type_id value_type_id{};
  field_flags flags = field_flags::none;
  std::string_view name{};
  property_kind kind = property_kind::invalid;
  property_capabilities capabilities = property_capabilities::none;
};
/** 自有字段数组；名称借用冻结 Registry 的文本，复制/移动不产生临时数组悬空。 */
struct type_info {
  type_id id{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::vector<field_info> fields{};
};
// NOLINTEND(readability-redundant-member-init)
/** 回调不得抛异常；user_data 必须存活到 Registry 关闭，访问器自行约定属性字符串寿命。 */
struct property_accessor_desc {
  property_kind kind = property_kind::invalid;
  result (*getter)(void*, property_target, property_value&) noexcept = nullptr;
  result (*setter)(void*, property_target, const property_value&) noexcept = nullptr;
  void* user_data = nullptr;
  friend bool operator==(const property_accessor_desc&,
                         const property_accessor_desc&) noexcept = default;
};
[[nodiscard]] constexpr gneiss_type_id to_native(const type_id& value) noexcept {
  gneiss_type_id native{};
  std::ranges::copy(value.bytes, native.bytes);
  return native;
}
[[nodiscard]] constexpr type_id from_native(const gneiss_type_id& value) noexcept {
  type_id native{};
  std::ranges::copy(value.bytes, native.bytes.begin());
  return native;
}
[[nodiscard]] constexpr gneiss_property_target to_native(property_target value) noexcept {
  return {.context = value.context, .object = value.object};
}
[[nodiscard]] constexpr property_target from_native(gneiss_property_target value) noexcept {
  return {.context = value.context, .object = value.object};
}
/** 按 variant 的活动类型转换，长度过大时保留原输出。 */
[[nodiscard]] inline result to_native(const property_value& value,
                                      gneiss_property_value& output) noexcept {
  gneiss_property_value native = GNEISS_PROPERTY_VALUE_INIT;
  native.kind = static_cast<std::uint32_t>(value.kind());
  switch (value.kind()) {
  case property_kind::invalid:
    return result::invalid_argument;
  case property_kind::boolean:
    native.payload.bool_value = static_cast<std::uint8_t>((*std::get_if<bool>(&value.payload)));
    break;
  case property_kind::int64:
    native.payload.int64_value = (*std::get_if<std::int64_t>(&value.payload));
    break;
  case property_kind::uint64:
    native.payload.uint64_value = (*std::get_if<std::uint64_t>(&value.payload));
    break;
  case property_kind::float32:
    native.payload.float32_value = (*std::get_if<float>(&value.payload));
    break;
  case property_kind::float64:
    native.payload.float64_value = (*std::get_if<double>(&value.payload));
    break;
  case property_kind::string: {
    const auto text = (*std::get_if<property_string>(&value.payload));
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
      return result::invalid_argument;
    }
    native.payload.string_value = {
        .data = text.data(),
        .length = static_cast<std::uint32_t>(text.size()),
    };
    break;
  }
  case property_kind::type_id:
    native.payload.type_id_value = to_native((*std::get_if<type_id>(&value.payload)));
    break;
  case property_kind::vec3: {
    const auto v = (*std::get_if<property_vec3>(&value.payload));
    native.payload.vec3_value = {.x = v.x, .y = v.y, .z = v.z};
    break;
  }
  case property_kind::quaternion: {
    const auto v = (*std::get_if<property_quaternion>(&value.payload));
    native.payload.quaternion_value = {.x = v.x, .y = v.y, .z = v.z, .w = v.w};
    break;
  }
  }
  output = native;
  return result::success;
}
/** C 值必须具有有效布局与文本指针；失败保留原输出。 */
[[nodiscard]] inline result from_native(const gneiss_property_value& value,
                                        property_value& output) noexcept {
  if (value.struct_size < sizeof(gneiss_property_value)) {
    return result::invalid_argument;
  }
  property_value native;
  switch (value.kind) {
  case GNEISS_PROPERTY_KIND_BOOL:
    if (value.payload.bool_value > 1) {
      return result::invalid_argument;
    }
    native.payload = value.payload.bool_value != 0;
    break;
  case GNEISS_PROPERTY_KIND_INT64:
    native.payload = value.payload.int64_value;
    break;
  case GNEISS_PROPERTY_KIND_UINT64:
    native.payload = value.payload.uint64_value;
    break;
  case GNEISS_PROPERTY_KIND_FLOAT32:
    native.payload = value.payload.float32_value;
    break;
  case GNEISS_PROPERTY_KIND_FLOAT64:
    native.payload = value.payload.float64_value;
    break;
  case GNEISS_PROPERTY_KIND_STRING: {
    const auto text = value.payload.string_value;
    if (text.length != 0 && text.data == nullptr) {
      return result::invalid_argument;
    }
    native.payload =
        text.data == nullptr ? property_string{} : property_string{text.data, text.length};
    break;
  }
  case GNEISS_PROPERTY_KIND_TYPE_ID:
    native.payload = from_native(value.payload.type_id_value);
    break;
  case GNEISS_PROPERTY_KIND_VEC3: {
    const auto v = value.payload.vec3_value;
    native.payload = property_vec3{.x = v.x, .y = v.y, .z = v.z};
    break;
  }
  case GNEISS_PROPERTY_KIND_QUATERNION: {
    const auto v = value.payload.quaternion_value;
    native.payload = property_quaternion{.x = v.x, .y = v.y, .z = v.z, .w = v.w};
    break;
  }
  default:
    return result::unsupported;
  }
  output = native;
  return result::success;
}
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
struct property_binding {
  type_id type;
  field_id field;
  property_accessor_desc callbacks;
};
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
class released_type_registry;

/** Type Registry 的独占 RAII 包装。查询名称借用 Registry；冻结前修改及关闭需外部同步。
 * 析构或移动覆盖若关闭失败则终止进程；需处理错误时先显式 reset()。 */
class type_registry final {
public:
  type_registry() noexcept = default;
  ~type_registry() noexcept { reset_or_terminate(); }

  type_registry(const type_registry&) = delete;
  type_registry& operator=(const type_registry&) = delete;

  type_registry(type_registry&& other) noexcept
      : handle_(std::exchange(other.handle_, 0)), bindings_(std::move(other.bindings_)) {}
  type_registry& operator=(type_registry&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      handle_ = std::exchange(other.handle_, 0);
      bindings_ = std::move(other.bindings_);
    }
    return *this;
  }

  [[nodiscard]] static result create(type_registry& output) noexcept {
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

  /** 同步复制类型与字段描述；失败不产生包装层的半注册状态。 */
  [[nodiscard]] result register_type(const type_desc& desc) const noexcept {
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
  /** 冻结前串行调用；描述被复制，user_data 仍由调用方持有至关闭。 */
  [[nodiscard]] result bind_property(type_id type, field_id field,
                                     const property_accessor_desc& desc) noexcept {
    for (const auto& binding : bindings_) {
      if (binding->type == type && binding->field == field && binding->callbacks == desc) {
        const auto native = detail::reflection_accessor(*binding);
        return bind_property_native(to_native(type), field.get(), native);
      }
    }
    try {
      auto candidate = std::make_unique<detail::property_binding>(
          detail::property_binding{.type = type, .field = field, .callbacks = desc});
      bindings_.reserve(bindings_.size() + 1);
      const auto native = detail::reflection_accessor(*candidate);
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
  /** 字段数组由输出拥有；名称借用 Registry，失败保持输出。 */
  [[nodiscard]] result type_at(std::uint32_t index, type_info& output) const noexcept {
    gneiss_type_info value = GNEISS_TYPE_INFO_INIT;
    const auto status = type_at_native(index, value);
    return status.failed() ? status : detail::reflection_type(value, output);
  }
  [[nodiscard]] result find_type(type_id id, type_info& output) const noexcept {
    gneiss_type_info value = GNEISS_TYPE_INFO_INIT;
    const auto status = find_type_native(to_native(id), value);
    return status.failed() ? status : detail::reflection_type(value, output);
  }
  [[nodiscard]] result find_field(type_id type, field_id field, field_info& output) const noexcept {
    gneiss_field_info value = GNEISS_FIELD_INFO_INIT;
    const auto status = find_field_native(to_native(type), field.get(), value);
    if (status.ok()) {
      output = detail::reflection_field(value);
    }
    return status;
  }
  [[nodiscard]] result get_property(type_id type, field_id field, property_target target,
                                    property_value& output) const noexcept {
    gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
    const auto status = get_property_native(to_native(type), field.get(), to_native(target), value);
    return status.failed() ? status : from_native(value, output);
  }
  [[nodiscard]] result set_property(type_id type, field_id field, property_target target,
                                    const property_value& value) const noexcept {
    gneiss_property_value native = GNEISS_PROPERTY_VALUE_INIT;
    const auto status = to_native(value, native);
    return status.failed()
               ? status
               : set_property_native(to_native(type), field.get(), to_native(target), native);
  }
  /** 显式 C ABI 互操作，描述和回调上下文保持原 C 契约。 */
  [[nodiscard]] result register_type_native(const gneiss_type_desc& desc) const noexcept {
    return from_native(gneiss_type_registry_register(handle_, &desc));
  }
  [[nodiscard]] result
  bind_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                       const gneiss_property_accessor_desc& desc) const noexcept {
    return from_native(gneiss_type_registry_bind_property(handle_, type_id, field_id, &desc));
  }
  [[nodiscard]] result freeze() const noexcept {
    return from_native(gneiss_type_registry_freeze(handle_));
  }
  /** 查询是否冻结；失败时不改变输出。 */
  [[nodiscard]] result is_frozen(bool& output) const noexcept {
    std::uint8_t value{};
    const auto status = from_native(gneiss_type_registry_is_frozen(handle_, &value));
    if (status.ok()) {
      output = value != 0U;
    }
    return status;
  }
  /** 按序号借用冻结 Registry 元数据，有效期不超过 Registry。 */
  [[nodiscard]] result type_at_native(std::uint32_t index,
                                      gneiss_type_info& output) const noexcept {
    output = GNEISS_TYPE_INFO_INIT;
    return from_native(gneiss_type_registry_type_at(handle_, index, &output));
  }
  [[nodiscard]] result type_count(std::uint32_t& output) const noexcept {
    return from_native(gneiss_type_registry_type_count(handle_, &output));
  }
  [[nodiscard]] result find_type_native(gneiss_type_id id,
                                        gneiss_type_info& output) const noexcept {
    output = GNEISS_TYPE_INFO_INIT;
    return from_native(gneiss_type_registry_find_type(handle_, id, &output));
  }
  [[nodiscard]] result find_field_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                         gneiss_field_info& output) const noexcept {
    output = GNEISS_FIELD_INFO_INIT;
    return from_native(gneiss_type_registry_find_field(handle_, type_id, field_id, &output));
  }
  [[nodiscard]] result get_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                           gneiss_property_target target,
                                           gneiss_property_value& output) const noexcept {
    return from_native(
        gneiss_type_registry_get_property(handle_, type_id, field_id, target, &output));
  }
  [[nodiscard]] result set_property_native(gneiss_type_id type_id, gneiss_field_id field_id,
                                           gneiss_property_target target,
                                           const gneiss_property_value& value) const noexcept {
    return from_native(
        gneiss_type_registry_set_property(handle_, type_id, field_id, target, &value));
  }

  [[nodiscard]] gneiss_type_registry get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != GNEISS_NULL_TYPE_REGISTRY;
  }

  /** 幂等关闭；无效句柄视为已释放，其他失败保留句柄；销毁与读写由调用方外部同步。
   * 返回结果可供检查；保留直接 reset() 的既有调用方式。 */
  result reset() noexcept {
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

  /** 同时转移 Registry 和稳定回调存储；载体析构关闭 Registry。 */
  [[nodiscard]] released_type_registry release() noexcept;
  /** 关闭原拥有者后接管载体；关闭失败保留两者。 */
  [[nodiscard]] static result adopt(released_type_registry&& source,
                                    type_registry& output) noexcept;

private:
  void reset_or_terminate() noexcept {
    if (reset().failed()) {
      std::terminate();
    }
  }
  gneiss_type_registry handle_ = GNEISS_NULL_TYPE_REGISTRY;
  std::vector<std::unique_ptr<detail::property_binding>> bindings_;
};

/** 独占释放载体，不隐式转为 C 句柄；get 只借用，禁止提前销毁仍在使用的 Registry。 */
class released_type_registry final {
public:
  released_type_registry() noexcept = default;
  released_type_registry(const released_type_registry&) = delete;
  released_type_registry& operator=(const released_type_registry&) = delete;
  released_type_registry(released_type_registry&&) noexcept = default;
  released_type_registry& operator=(released_type_registry&&) noexcept = default;
  [[nodiscard]] gneiss_type_registry get() const noexcept { return owner_.get(); }
  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(owner_); }
  result reset() noexcept { return owner_.reset(); }

private:
  friend class type_registry;
  explicit released_type_registry(type_registry&& owner) noexcept : owner_(std::move(owner)) {}
  type_registry owner_;
};
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

} // namespace gneiss

#endif
