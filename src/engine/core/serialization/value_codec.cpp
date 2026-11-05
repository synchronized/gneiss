// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/detail/traversal.hpp"
#include <algorithm>
#include <array>
namespace gneiss::engine::serialization::detail {
using namespace meta;
serialization_diagnostic failure(error, std::string = {}, std::string = {});
serialization_diagnostic prefix(serialization_diagnostic, const std::string&);
constexpr std::array<std::string_view, 13> value_names{
    "",     "bool", "int64",      "uint64", "float32", "float64", "string",
    "guid", "vec3", "quaternion", "vec4",   "vec2",    "object"};
result<value, serialization_diagnostic> decode_value(const context& ctx, reader& source,
                                                     reader::node input) noexcept try {
  auto* previous = context_access::input(ctx);
  struct restore {
    const context& ctx;
    reader* previous;
    ~restore() { context_access::input(ctx) = previous; }
  } restore_input{ctx, previous};
  context_access::input(ctx) = &source;
  if (!source.is_object(input) || source.size(input) != 2)
    return failure(error::invalid_argument);
  auto* tag = source.member(input, "kind");
  auto* content = source.member(input, "value");
  if (!source.is_string(tag) || !content)
    return failure(error::invalid_argument);
  const auto name = source.string(tag);
  const auto found = std::ranges::find(value_names, name);
  if (found == value_names.end() || found == value_names.begin())
    return failure(error::unsupported);
  if (name == "object") {
    if (source.is_null(content))
      return value{ref<meta::object>{}};
    auto* tag = source.member(content, "$type_guid");
    auto* fields = source.member(content, "$content");
    if (!source.is_object(content) || source.size(content) != 2 || !source.is_string(tag) ||
        !source.is_object(fields))
      return failure(error::invalid_argument, "/value", "对象需要类型和内容");
    auto guid = engine::guid::parse(source.string(tag));
    if (!guid || !*guid || guid->string() != source.string(tag))
      return failure(error::invalid_argument, "/value/$type_guid");
    auto selected = ctx.registry();
    if (!selected)
      return failure(selected.error());
    const auto& registry = *selected;
    auto type = describe_type(registry, *guid);
    if (!type)
      return failure(type.error(), "/value/$type_guid");
    auto binding = find_serialization(registry, type->type_id);
    if (!binding || !binding->as_object)
      return failure(error::unsupported, "/value", "类型不是可反射对象");
    auto decoded = decode_owned(ctx, registry, type->type_id, fields, context_access::depth(ctx));
    if (!decoded)
      return prefix(std::move(decoded).error(), "/value/$content");
    auto* object = binding->as_object(decoded->get());
    if (!object || object->dynamic_type() != type->type_id ||
        object->dynamic_guid() != type->guid || object->dynamic_address() != decoded->get())
      return failure(error::invalid_argument, "/value", "对象实际身份与注册类型不符");
    return value{decoded->alias(object)};
  }
  auto decoded = source.scalar(content, static_cast<value_kind>(found - value_names.begin()));
  if (!decoded)
    return prefix(std::move(decoded).error(), "/value");
  if (!valid_scalar(*decoded))
    return failure(error::invalid_argument, "/value");
  return decoded;
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
result<writer::node, serialization_diagnostic> encode_value(const context& ctx, writer& output,
                                                            const value& input) noexcept try {
  if (input.kind() != value_kind::object && !valid_scalar(input))
    return failure(error::invalid_argument);
  auto content = [&]() -> result<writer::node, serialization_diagnostic> {
    if (input.kind() != value_kind::object)
      return output.scalar(input);
    const auto& owner = std::get<ref<meta::object>>(input.payload);
    if (!owner)
      return output.null();
    auto selected = ctx.registry();
    if (!selected)
      return failure(selected.error());
    const auto& registry = *selected;
    auto type = describe_type(registry, owner->dynamic_type());
    if (!type)
      return failure(type.error(), "/value/$type_guid");
    auto binding = find_serialization(registry, owner->dynamic_type());
    if (!binding || !binding->as_object || type->guid != owner->dynamic_guid())
      return failure(error::unsupported, "/value");
    const auto* address = owner->dynamic_address();
    if (context_access::depth(ctx) > ctx.maximum_depth() ||
        !context_access::active(ctx).insert(address).second)
      return failure(error::invalid_argument, "/value", "引用环或嵌套深度超限");
    active_reference reference{ctx, address};
    auto fields = encode_nested(ctx, registry, owner->dynamic_type(), address, &output,
                                context_access::depth(ctx) + 1);

    if (!fields)
      return prefix(std::move(fields).error(), "/value/$content");
    auto* envelope = output.object();
    const auto guid = type->guid.string();
    if (!envelope || !output.member(envelope, "$type_guid", output.string(guid)) ||
        !output.member(envelope, "$content", *fields))
      return failure(error::out_of_memory);
    return envelope;
  }();
  if (!content)
    return std::move(content).error();
  auto* object = output.object();
  const auto name = value_names[static_cast<std::size_t>(input.kind())];
  if (!object || !output.member(object, "kind", output.string(name)) ||
      !output.member(object, "value", *content))
    return failure(error::out_of_memory);
  return object;
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

}
namespace gneiss::engine::meta {
error register_builtin_types(const registry_ref& registry) noexcept {
  return type_builder<value>{registry}
      .serialization<&serialization::detail::encode_value, &serialization::detail::decode_value>()
      .validate<[](const value& v) {
        return v.kind() == value_kind::object || valid_scalar(v) ? error::success
                                                                 : error::invalid_argument;
      }>()
      .commit();
}
}
