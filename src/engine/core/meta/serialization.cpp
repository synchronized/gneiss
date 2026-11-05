// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/serialization.hpp"
#include "engine/core/meta/detail/property_view_access.hpp"
#include "engine/core/meta/detail/reference_view_access.hpp"
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/json_serialization.hpp"
#include "engine/core/meta/registration.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_set>
#include <yyjson.h>

namespace gneiss::engine::meta {
namespace {
struct serialization_context {
  std::unordered_set<const void*> active;
};
}
namespace detail {
struct json_codec_state {
  const registry_ref& registry;
  serialization_context& traversal;
  std::size_t depth;
  json_codec_context context() { return json_codec_context{*this}; }
  static result<yyjson_mut_val*, serialization_diagnostic>
  write_fields(json_codec_context& ctx, yyjson_mut_doc* doc, type_id id, const void* object) {
    return ctx.write_fields_object(doc, id, object);
  }
  static result<ref<void>, serialization_diagnostic>
  read_fields(json_codec_context& ctx, yyjson_val* input, type_id id,
              std::span<const std::string_view> ignored) {
    return ctx.read_fields_object(input, id, ignored);
  }
};
}
namespace {
std::string field_path(std::string_view name) {
  std::string path = "/";
  for (char c : name) {
    if (c == '~')
      path += "~0";
    else if (c == '/')
      path += "~1";
    else
      path += c;
  }
  return path;
}
serialization_diagnostic failure(error code, std::string path = {}, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = code};
}
result<value, serialization_diagnostic> read_value(yyjson_val* input, value_kind kind,
                                                   std::string path) {
  value output;
  switch (kind) {
  case value_kind::boolean:
    if (yyjson_is_bool(input)) {
      output.payload = yyjson_get_bool(input);
      return output;
    }
    break;
  case value_kind::int64:
    if (yyjson_is_sint(input)) {
      output.payload = yyjson_get_sint(input);
      return output;
    }
    if (yyjson_is_uint(input) &&
        yyjson_get_uint(input) <=
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      output.payload = static_cast<std::int64_t>(yyjson_get_uint(input));
      return output;
    }
    break;
  case value_kind::uint64:
    if (yyjson_is_uint(input)) {
      output.payload = yyjson_get_uint(input);
      return output;
    }
    break;
  case value_kind::float32:
  case value_kind::float64:
    if (yyjson_is_num(input) && std::isfinite(yyjson_get_num(input))) {
      const auto number = yyjson_get_num(input);
      if (kind == value_kind::float64) {
        output.payload = number;
        return output;
      }
      if (std::abs(number) <= std::numeric_limits<float>::max()) {
        output.payload = static_cast<float>(number);
        return output;
      }
    }
    break;
  case value_kind::string:
    if (yyjson_is_str(input)) {
      output.payload = std::string(yyjson_get_str(input), yyjson_get_len(input));
      return output;
    }
    break;
  case value_kind::type_guid:
    if (yyjson_is_null(input)) {
      output.payload = guid{};
      return output;
    }
    if (yyjson_is_str(input)) {
      const std::string_view text{yyjson_get_str(input), yyjson_get_len(input)};
      auto id = guid::parse(text);
      if (id && id->string() == text) {
        output.payload = *id;
        return output;
      }
    }
    break;
  case value_kind::vec2:
  case value_kind::vec3:
  case value_kind::vec4:
  case value_kind::quaternion: {
    const std::size_t count = kind == value_kind::vec2 ? 2U : kind == value_kind::vec3 ? 3U : 4U;
    if (!yyjson_is_arr(input) || yyjson_arr_size(input) != count)
      break;
    math::quaternion numbers{};
    auto components = math::components(numbers);
    for (std::size_t i = 0; i < count; ++i) {
      auto* item = yyjson_arr_get(input, i);
      if (!yyjson_is_num(item) || !std::isfinite(yyjson_get_num(item)) ||
          std::abs(yyjson_get_num(item)) > std::numeric_limits<float>::max())
        return failure(error::invalid_argument, path + "/" + std::to_string(i),
                       "分量必须为有限 float");
      components[i] = static_cast<float>(yyjson_get_num(item));
    }
    if (kind == value_kind::vec2)
      output.payload = math::vector2{components[0], components[1]};
    else if (kind == value_kind::vec3)
      output.payload = math::vector3{components[0], components[1], components[2]};
    else if (kind == value_kind::vec4)
      output.payload = math::vector4{components[0], components[1], components[2], components[3]};
    else
      output.payload = numbers;
    return output;
  }
  default:
    return failure(error::unsupported, std::move(path), "不支持的反射属性类型");
  }
  return failure(error::invalid_argument, std::move(path), "属性 JSON 类型或取值范围错误");
}
result<yyjson_mut_val*, serialization_diagnostic>
write_value(yyjson_mut_doc* doc, const value& input, const std::string& path) {
  return std::visit(
      [&](const auto& item) -> result<yyjson_mut_val*, serialization_diagnostic> {
        using T = std::decay_t<decltype(item)>;
        yyjson_mut_val* output = nullptr;
        if constexpr (std::is_same_v<T, std::monostate>)
          return failure(error::unsupported, path);
        else if constexpr (std::is_same_v<T, bool>)
          output = yyjson_mut_bool(doc, item);
        else if constexpr (std::is_same_v<T, std::int64_t>)
          output = yyjson_mut_sint(doc, item);
        else if constexpr (std::is_same_v<T, std::uint64_t>)
          output = yyjson_mut_uint(doc, item);
        else if constexpr (std::is_floating_point_v<T>) {
          if (!std::isfinite(item))
            return failure(error::invalid_argument, path, "不能保存非有限浮点值");
          output = yyjson_mut_real(doc, item);
        } else if constexpr (std::is_same_v<T, std::string>)
          output = yyjson_mut_strncpy(doc, item.data(), item.size());
        else if constexpr (std::is_same_v<T, guid>) {
          if (!item) {
            output = yyjson_mut_null(doc);
          } else {
            auto text = item.string();
            output = yyjson_mut_strncpy(doc, text.data(), text.size());
          }
        } else {
          output = yyjson_mut_arr(doc);
          if (!output)
            return failure(error::out_of_memory, path);
          for (float number : math::components(item)) {
            if (!std::isfinite(number))
              return failure(error::invalid_argument, path, "不能保存非有限分量");
            if (!yyjson_mut_arr_add_real(doc, output, number))
              return failure(error::out_of_memory, path);
          }
        }
        if (!output)
          return failure(error::out_of_memory, path);
        return output;
      },
      input.payload);
}

serialization_diagnostic prefix(serialization_diagnostic diagnostic, const std::string& path) {
  diagnostic.path = path + diagnostic.path;
  // 子 codec 的偏移相对于重编码片段，不冒充完整文档偏移。
  diagnostic.byte_offset = 0;
  return diagnostic;
}
/** 检查注册继承链并按 C++ 转换逐层调整地址。 */
result<void*, serialization_diagnostic> cast_base(const registry_ref& registry, type_id actual,
                                                  type_id declared, void* pointer) {
  for (std::size_t depth = 0; depth <= 64; ++depth) {
    if (actual == declared)
      return pointer;
    auto binding = find_serialization(registry, actual);
    if (!binding || !binding->base_type || !binding->upcast)
      return failure(error::invalid_argument, "", "实际类型不属于声明基类");
    if (pointer)
      pointer = binding->upcast(pointer);
    actual = binding->base_type;
  }
  return failure(error::invalid_argument, "", "继承链过深或存在环");
}
result<yyjson_mut_val*, serialization_diagnostic> encode_members(serialization_context& ctx,
                                                                 const registry_ref&, type_id,
                                                                 const void*, yyjson_mut_doc*,
                                                                 std::size_t);
result<yyjson_mut_val*, serialization_diagnostic>
encode_nested(serialization_context& ctx, const registry_ref& registry, type_id id,
              const void* object, yyjson_mut_doc* doc, std::size_t depth) {
  if (depth > 64)
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  if (!object)
    return failure(error::internal);
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  if (binding->validate) {
    auto status = binding->validate(object);
    if (status.error.failed())
      return status;
  }
  if (binding->encode_json) {
    detail::json_codec_state state{registry, ctx, depth};
    auto context = state.context();
    auto encoded = binding->encode_json(context, doc, object);
    if (!encoded)
      return std::move(encoded).error();
    if (!yyjson_mut_is_obj(*encoded))
      return failure(error::invalid_argument, "", "嵌套 codec 必须输出 JSON 对象");
    return encoded;
  }
  if (binding->encode) {
    auto encoded = binding->encode(registry, object);
    if (!encoded)
      return std::move(encoded).error();
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> parsed(
        yyjson_read(reinterpret_cast<const char*>(encoded->data()), encoded->size(), 0),
        &yyjson_doc_free);
    if (!parsed || !yyjson_is_obj(yyjson_doc_get_root(parsed.get())))
      return failure(error::invalid_argument, "", "嵌套 codec 必须输出 JSON 对象");
    auto* copied = yyjson_val_mut_copy(doc, yyjson_doc_get_root(parsed.get()));
    if (!copied)
      return failure(error::out_of_memory);
    return copied;
  }
  if (!binding->reflected_json)
    return failure(error::unsupported);
  return encode_members(ctx, registry, id, object, doc, depth);
}
result<yyjson_mut_val*, serialization_diagnostic> encode_nonobject(serialization_context&,
                                                                   const registry_ref&,
                                                                   const instance&, yyjson_mut_doc*,
                                                                   std::size_t);
/** 对象递归使用轻量栈帧，不携带各容器分支的临时状态。 */
result<yyjson_mut_val*, serialization_diagnostic>
encode_view(serialization_context& ctx, const registry_ref& registry, const instance& view,
            yyjson_mut_doc* doc, std::size_t depth) {
  if (view.kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return encode_nested(ctx, registry, view.type().id(), *p, doc, depth);
  }
  return encode_nonobject(ctx, registry, view, doc, depth);
}
result<yyjson_mut_val*, serialization_diagnostic>
encode_nonobject(serialization_context& ctx, const registry_ref& registry, const instance& view,
                 yyjson_mut_doc* doc, std::size_t depth) {
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::reference) {
    auto owned = detail::reference_view_access::owner(view);
    if (!owned)
      return failure(owned.error());
    if (!*owned)
      return yyjson_mut_null(doc);
    auto target = value_type.element_type();
    if (!target)
      return failure(target.error());
    if (target->kind() != type_kind::object)
      return failure(error::unsupported, "", "内嵌引用目标必须是注册对象");
    auto declared = find_serialization(registry, target->id());
    auto actual = detail::reference_view_access::dynamic_type(view);
    if (!declared || !actual)
      return failure(error::invalid_argument);
    if (!declared->polymorphic && *actual != target->id())
      return failure(error::invalid_argument, "", "未声明多态的引用不能保存派生对象");
    auto adjusted = cast_base(registry, *actual, target->id(), nullptr);
    if (!adjusted)
      return std::move(adjusted).error();
    auto binding = find_serialization(registry, *actual);
    if (!binding || (!binding->reflected_json && !binding->encode && !binding->encode_json))
      return failure(error::unsupported, "", "引用目标缺少编码器");
    const auto* address = owned->get();
    if (depth > 64 || !ctx.active.insert(address).second)
      return failure(error::invalid_argument, "", "引用环或嵌套深度超限");
    auto content = encode_nested(ctx, registry, *actual, address, doc, depth + 1);
    ctx.active.erase(address);
    if (!content)
      return std::move(content).error();
    if (!declared->polymorphic)
      return content;
    auto actual_type = describe_type(registry, *actual);
    if (!actual_type)
      return failure(actual_type.error());
    auto* entry = yyjson_mut_obj(doc);
    const auto guid = actual_type->guid.string();
    if (!entry || !yyjson_mut_obj_add_strcpy(doc, entry, "$type_guid", guid.c_str()) ||
        !yyjson_mut_obj_add_val(doc, entry, "$content", *content))
      return failure(error::out_of_memory);
    return entry;
  }
  if (depth > 64 && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? write_value(doc, *v, "")
             : result<yyjson_mut_val*, serialization_diagnostic>{failure(v.error())};
  }
  if (value_type.kind() == type_kind::variant) {
    auto variant = view.as_variant();
    if (!variant)
      return failure(variant.error());
    if (variant->names().empty())
      return failure(error::unsupported, "", "variant 未配置文件标签");
    auto index = variant->index();
    if (!index)
      return failure(index.error());
    auto child = variant->value();
    if (!child)
      return failure(child.error());
    auto content = encode_view(ctx, registry, *child, doc, depth + 1);
    if (!content)
      return prefix(std::move(content).error(), "/$value");
    auto* output = yyjson_mut_obj(doc);
    auto name = variant->names()[*index];
    if (!output || !yyjson_mut_obj_add_strncpy(doc, output, "$case", name.data(), name.size()) ||
        !yyjson_mut_obj_add_val(doc, output, "$value", *content))
      return failure(error::out_of_memory);
    return output;
  }
  if (value_type.kind() == type_kind::optional) {
    auto v = view.as_optional();
    if (!v)
      return failure(v.error());
    auto has = v->has_value();
    if (!has)
      return failure(has.error());
    if (!*has) {
      auto* output = yyjson_mut_null(doc);
      return output ? result<yyjson_mut_val*, serialization_diagnostic>{output}
                    : failure(error::out_of_memory);
    }
    auto child = v->get();
    if (!child)
      return failure(child.error());
    auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
    if (encoded && yyjson_mut_is_null(*encoded))
      return failure(error::unsupported, "", "optional 的非空值不能编码为 null");
    return encoded;
  }
  if (value_type.kind() == type_kind::sequence) {
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    auto count = v->size();
    if (!count)
      return failure(count.error());
    auto* output = yyjson_mut_arr(doc);
    if (!output)
      return failure(error::out_of_memory);
    for (std::size_t i = 0; i < *count; ++i) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
      if (!encoded)
        return prefix(std::move(encoded).error(), "/" + std::to_string(i));
      if (!yyjson_mut_arr_append(output, *encoded))
        return failure(error::out_of_memory);
    }
    return output;
  }
  if (value_type.kind() == type_kind::associative) {
    auto v = view.as_associative();
    if (!v)
      return failure(v.error());
    auto keys = v->keys();
    if (!keys)
      return failure(keys.error());
    auto* output = yyjson_mut_obj(doc);
    if (!output)
      return failure(error::out_of_memory);
    for (const auto& key : *keys) {
      auto child = v->find(key);
      if (!child)
        return failure(child.error());
      auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
      if (!encoded)
        return prefix(std::move(encoded).error(), field_path(key));
      auto* name = yyjson_mut_strncpy(doc, key.data(), key.size());
      if (!name || !yyjson_mut_obj_add(output, name, *encoded))
        return failure(error::out_of_memory);
    }
    return output;
  }
  return failure(error::unsupported);
}
result<yyjson_mut_val*, serialization_diagnostic> encode_enum_name(const instance& view,
                                                                   yyjson_mut_doc* doc) {
  auto entries = view.type().enum_entries();
  auto scalar = view.read_value();
  if (!entries || !scalar)
    return failure(error::invalid_argument, "", "属性必须是已注册枚举");
  const auto found = std::ranges::find(*entries, *scalar, &enum_entry::data);
  if (found == entries->end())
    return failure(error::invalid_argument, "", "未知枚举值");
  auto* name = yyjson_mut_strncpy(doc, found->name.data(), found->name.size());
  if (!name)
    return failure(error::out_of_memory);
  return name;
}
serialization_diagnostic decode_enum_name(const mutable_instance& view, yyjson_val* input) {
  auto entries = view.type().enum_entries();
  if (!entries || !yyjson_is_str(input))
    return failure(error::invalid_argument, "", "属性必须是枚举名称");
  const std::string_view name{yyjson_get_str(input), yyjson_get_len(input)};
  const auto found = std::ranges::find(*entries, name, &enum_entry::name);
  if (found == entries->end())
    return failure(error::invalid_argument, "", "未知枚举名称");
  auto status = view.write_value(found->data);
  return status.failed() ? failure(status) : serialization_diagnostic{};
}
result<yyjson_mut_val*, serialization_diagnostic>
encode_members(serialization_context& ctx, const registry_ref& registry, type_id id,
               const void* object, yyjson_mut_doc* doc, std::size_t depth) {
  auto view = instance::from_object(registry, id, object);
  if (!view)
    return failure(view.error());
  auto properties = view->properties();
  if (!properties)
    return failure(properties.error());
  auto* root = yyjson_mut_obj(doc);
  if (!root)
    return failure(error::out_of_memory);
  for (const auto& property : *properties) {
    if (has_flags(property.flags, property_flags::read_only))
      continue;
    const auto path = field_path(property.name);
    auto child = view->property(property.id);
    if (!child)
      return failure(child.error(), path);
    auto encoded = has_flags(property.flags, property_flags::enum_name)
                       ? encode_enum_name(*child, doc)
                       : encode_view(ctx, registry, *child, doc, depth + 1);
    if (!encoded)
      return prefix(std::move(encoded).error(), path);
    auto* key = yyjson_mut_strncpy(doc, property.name.data(), property.name.size());
    if (!key || !yyjson_mut_obj_add(root, key, *encoded))
      return failure(error::out_of_memory);
  }
  return root;
}
result<std::vector<std::byte>, serialization_diagnostic>
encode_json(const registry_ref& registry, type_id id, const void* object, bool custom = false) {
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return failure(error::out_of_memory);
  serialization_context ctx;
  auto root = custom ? encode_nested(ctx, registry, id, object, doc.get(), 0)
                     : encode_members(ctx, registry, id, object, doc.get(), 0);
  if (!root)
    return std::move(root).error();
  yyjson_mut_doc_set_root(doc.get(), *root);
  std::size_t size{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &size),
                                                   &std::free);
  if (!text)
    return failure(error::out_of_memory);
  if (custom) {
    auto binding = find_serialization(registry, id);
    if (binding && size > binding->maximum_json_bytes)
      return failure(error::invalid_argument, "", "资源描述超过文件大小上限");
  }
  auto bytes = std::as_bytes(std::span{text.get(), size});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
serialization_diagnostic validate_defaults(const registry_ref&, type_id, const void*, std::size_t);
serialization_diagnostic validate_view(const registry_ref& registry, const instance& view,
                                       std::size_t depth) {
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::reference) {
    auto owner = detail::reference_view_access::owner(view);
    return owner && !*owner ? serialization_diagnostic{}
                            : failure(error::unsupported, "", "非空缺省引用必须显式保存");
  }
  if (depth > 64 && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return validate_defaults(registry, value_type.id(), *p, depth);
  }
  if (value_type.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? serialization_diagnostic{} : failure(v.error());
  }
  if (value_type.kind() == type_kind::variant) {
    auto variant = view.as_variant();
    if (!variant)
      return failure(variant.error());
    auto child = variant->value();
    if (!child)
      return failure(child.error());
    auto status = validate_view(registry, *child, depth + 1);
    return status.error.failed() ? prefix(std::move(status), "/$value") : status;
  }
  if (value_type.kind() == type_kind::optional) {
    auto v = view.as_optional();
    if (!v)
      return failure(v.error());
    auto has = v->has_value();
    if (!has)
      return failure(has.error());
    if (!*has)
      return {};
    auto child = v->get();
    if (!child)
      return failure(child.error());
    return validate_view(registry, *child, depth + 1);
  }
  if (value_type.kind() == type_kind::sequence) {
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    auto n = v->size();
    if (!n)
      return failure(n.error());
    for (std::size_t i = 0; i < *n; ++i) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      auto status = validate_view(registry, *child, depth + 1);
      if (status.error.failed())
        return prefix(std::move(status), "/" + std::to_string(i));
    }
    return {};
  }
  auto v = view.as_associative();
  if (!v)
    return failure(v.error());
  auto keys = v->keys();
  if (!keys)
    return failure(keys.error());
  for (const auto& key : *keys) {
    auto child = v->find(key);
    if (!child)
      return failure(child.error());
    auto status = validate_view(registry, *child, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), field_path(key));
  }
  return {};
}
serialization_diagnostic validate_defaults(const registry_ref& registry, type_id id,
                                           const void* object, std::size_t depth) {
  if (depth > 64)
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  if (binding->validate) {
    auto status = binding->validate(object);
    if (status.error.failed())
      return status;
  }
  if (!binding->reflected_json)
    return {};
  auto view = instance::from_object(registry, id, object);
  if (!view)
    return failure(view.error());
  auto properties = view->properties();
  if (!properties)
    return failure(properties.error());
  for (const auto& property : *properties) {
    if (property.type.kind() == type_kind::scalar ||
        has_flags(property.flags, property_flags::read_only))
      continue;
    auto child = view->property(property.id);
    if (!child)
      return failure(child.error(), field_path(property.name));
    auto status = validate_view(registry, *child, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), field_path(property.name));
  }
  return {};
}
serialization_diagnostic decode_members(serialization_context& ctx, const registry_ref&, type_id,
                                        void*, yyjson_val*, std::size_t,
                                        std::span<const std::string_view> = {});
serialization_diagnostic decode_nested(serialization_context& ctx, const registry_ref& registry,
                                       const mutable_instance& target, yyjson_val* input,
                                       std::size_t depth) {
  if (depth > 64)
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  if (!yyjson_is_obj(input))
    return failure(error::invalid_argument, "", "嵌套值必须是 JSON 对象");
  auto binding = find_serialization(registry, target.type().id());
  if (!binding)
    return failure(binding.error());
  if (binding->decode_json) {
    detail::json_codec_state state{registry, ctx, depth};
    auto context = state.context();
    auto value = binding->decode_json(context, input);
    if (!value)
      return std::move(value).error();
    if (!*value)
      return failure(error::internal);
    if (binding->validate) {
      auto status = binding->validate(value->get());
      if (status.error.failed())
        return status;
    }
    auto status = target.assign_object(target.type().id(), value->get());
    return status.failed() ? failure(status) : serialization_diagnostic{};
  }
  if (binding->decode) {
    std::size_t size{};
    std::unique_ptr<char, decltype(&std::free)> text(yyjson_val_write(input, 0, &size), &std::free);
    if (!text)
      return failure(error::out_of_memory);
    auto value = binding->decode(registry, std::as_bytes(std::span{text.get(), size}));
    if (!value)
      return std::move(value).error();
    if (!*value)
      return failure(error::internal);
    if (binding->validate) {
      auto status = binding->validate(value->get());
      if (status.error.failed())
        return status;
    }
    const auto assigned = target.assign_object(target.type().id(), value->get());
    return assigned.failed() ? failure(assigned) : serialization_diagnostic{};
  }
  if (!binding->reflected_json)
    return failure(error::unsupported);
  auto address = target.object_data();
  if (!address)
    return failure(address.error());
  auto* object = *address;
  auto status = decode_members(ctx, registry, target.type().id(), object, input, depth);
  if (status.error.failed())
    return status;
  return binding->validate ? binding->validate(object) : serialization_diagnostic{};
}
serialization_diagnostic decode_nonobject(serialization_context&, const registry_ref&,
                                          const mutable_instance&, yyjson_val*, std::size_t);
serialization_diagnostic decode_view(serialization_context& ctx, const registry_ref& registry,
                                     const mutable_instance& view, yyjson_val* input,
                                     std::size_t depth) {
  if (view.kind() == type_kind::object && !detail::property_view_access::requires_replacement(view))
    return decode_nested(ctx, registry, view, input, depth);
  return decode_nonobject(ctx, registry, view, input, depth);
}
serialization_diagnostic decode_nonobject(serialization_context& ctx, const registry_ref& registry,
                                          const mutable_instance& view, yyjson_val* input,
                                          std::size_t depth) {
  if (detail::property_view_access::requires_replacement(view)) {
    serialization_diagnostic diagnostic;
    auto status =
        detail::property_view_access::replace(view, [&](const mutable_instance& temporary) {
          diagnostic = decode_view(ctx, registry, temporary, input, depth);
          return diagnostic.error;
        });
    return diagnostic.error.failed()
               ? diagnostic
               : (status.failed() ? failure(status) : serialization_diagnostic{});
  }
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::reference) {
    if (yyjson_is_null(input)) {
      auto status = detail::reference_view_access::reset(view);
      return status.failed() ? failure(status) : serialization_diagnostic{};
    }
    auto target = value_type.element_type();
    if (!target || target->kind() != type_kind::object)
      return failure(error::unsupported);
    auto declared = find_serialization(registry, target->id());
    if (!declared)
      return failure(declared.error());
    auto actual = target->id();
    auto* content = input;
    if (declared->polymorphic) {
      auto* tag = yyjson_obj_get(input, "$type_guid");
      content = yyjson_obj_get(input, "$content");
      if (!yyjson_is_obj(input) || yyjson_obj_size(input) != 2 || !yyjson_is_str(tag) ||
          !yyjson_is_obj(content))
        return failure(error::invalid_argument, "", "多态引用需要类型和内容");
      auto guid = engine::guid::parse({yyjson_get_str(tag), yyjson_get_len(tag)});
      if (!guid || !*guid ||
          guid->string() != std::string_view{yyjson_get_str(tag), yyjson_get_len(tag)})
        return failure(error::invalid_argument, "/$type_guid");
      auto type = describe_type(registry, *guid);
      if (!type)
        return failure(type.error(), "/$type_guid");
      actual = type->type_id;
    }
    auto compatible = cast_base(registry, actual, target->id(), nullptr);
    if (!compatible)
      return std::move(compatible).error();
    if (depth > 64)
      return failure(error::invalid_argument, "", "引用嵌套深度超限");
    auto binding = find_serialization(registry, actual);
    if (!binding)
      return failure(binding.error());
    ref<void> object;
    serialization_diagnostic status;
    if (binding->decode_json) {
      detail::json_codec_state state{registry, ctx, depth + 1};
      auto context = state.context();
      auto decoded = binding->decode_json(context, content);
      if (!decoded)
        status = std::move(decoded).error();
      else
        object = std::move(*decoded);
    } else if (binding->decode) {
      std::size_t size{};
      std::unique_ptr<char, decltype(&std::free)> text(yyjson_val_write(content, 0, &size),
                                                       &std::free);
      if (!text)
        return failure(error::out_of_memory);
      auto decoded = binding->decode(registry, std::as_bytes(std::span{text.get(), size}));
      if (!decoded)
        status = std::move(decoded).error();
      else
        object = std::move(*decoded);
    } else {
      if (!binding->reflected_json || !binding->construct)
        return failure(error::unsupported);
      object = binding->construct();
      if (!object)
        return failure(error::internal);
      status = decode_members(ctx, registry, actual, object.get(), content, depth + 1);
    }
    if (status.error.ok() && !object)
      status = failure(error::internal);
    if (status.error.ok() && binding->validate)
      status = binding->validate(object.get());
    if (status.error.failed())
      return declared->polymorphic ? prefix(std::move(status), "/$content") : status;
    auto adjusted = cast_base(registry, actual, target->id(), object.get());
    if (!adjusted)
      return std::move(adjusted).error();
    auto assigned =
        detail::reference_view_access::assign(view, target->id(), object.alias(*adjusted));
    return assigned.failed() ? failure(assigned) : serialization_diagnostic{};
  }
  if (depth > 64 && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::scalar) {
    auto decoded = read_value(input, value_type.scalar_kind(), "");
    if (!decoded)
      return std::move(decoded).error();
    auto status = view.write_value(*decoded);
    return status.failed() ? failure(status) : serialization_diagnostic{};
  }
  if (value_type.kind() == type_kind::variant) {
    auto variant = view.as_variant();
    if (!variant)
      return failure(variant.error());
    if (variant->names().empty())
      return failure(error::unsupported, "", "variant 未配置文件标签");
    auto* tag = yyjson_is_obj(input) ? yyjson_obj_get(input, "$case") : nullptr;
    auto* content = yyjson_is_obj(input) ? yyjson_obj_get(input, "$value") : nullptr;
    if (!yyjson_is_obj(input) || yyjson_obj_size(input) != 2 || !yyjson_is_str(tag) || !content)
      return failure(error::invalid_argument, "", "variant 必须包含 $case 和 $value");
    const std::string_view name{yyjson_get_str(tag), yyjson_get_len(tag)};
    auto names = variant->names();
    auto found = std::ranges::find(names, name);
    if (found == names.end())
      return failure(error::invalid_argument, "/$case", "未知 variant 标签");
    auto status = variant->select(static_cast<std::size_t>(found - names.begin()));
    if (status.failed())
      return failure(status, "/$case");
    auto child = variant->value();
    if (!child)
      return failure(child.error());
    auto decoded = decode_view(ctx, registry, *child, content, depth + 1);
    return decoded.error.failed() ? prefix(std::move(decoded), "/$value") : decoded;
  }
  if (value_type.kind() == type_kind::optional) {
    auto v = view.as_optional();
    if (!v)
      return failure(v.error());
    auto status = v->reset();
    if (status.failed())
      return failure(status);
    if (yyjson_is_null(input))
      return {};
    auto child = v->ensure();
    if (!child)
      return failure(child.error());
    return decode_view(ctx, registry, *child, input, depth + 1);
  }
  if (value_type.kind() == type_kind::sequence) {
    if (!yyjson_is_arr(input))
      return failure(error::invalid_argument, "", "序列必须是 JSON 数组");
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    if (v->supports_replacement()) {
      serialization_diagnostic decoded;
      auto status = v->replace([&](const mutable_instance& candidate) {
        decoded = decode_view(ctx, registry, candidate, input, depth);
        return decoded.error;
      });
      return decoded.error.failed() ? decoded : failure(status);
    }
    if (v->is_resizable()) {
      auto status = v->resize(0);
      if (status.failed())
        return failure(status);
    }
    auto status = v->resize(yyjson_arr_size(input));
    if (status.failed())
      return failure(status);
    std::size_t i = 0;
    auto it = yyjson_arr_iter_with(input);
    while (auto* item = yyjson_arr_iter_next(&it)) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      auto decoded = decode_view(ctx, registry, *child, item, depth + 1);
      if (decoded.error.failed())
        return prefix(std::move(decoded), "/" + std::to_string(i));
      ++i;
    }
    return {};
  }
  if (value_type.kind() == type_kind::associative) {
    if (!yyjson_is_obj(input))
      return failure(error::invalid_argument, "", "关联容器必须是 JSON 对象");
    auto v = view.as_associative();
    if (!v)
      return failure(v.error());
    auto status = v->clear();
    if (status.failed())
      return failure(status);
    std::unordered_set<std::string_view> keys;
    auto it = yyjson_obj_iter_with(input);
    while (auto* key = yyjson_obj_iter_next(&it)) {
      const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
      if (!keys.insert(name).second)
        return failure(error::invalid_argument, field_path(name), "重复关联键");
      auto child = v->insert(name);
      if (!child)
        return failure(child.error(), field_path(name));
      auto decoded = decode_view(ctx, registry, *child, yyjson_obj_iter_get_val(key), depth + 1);
      if (decoded.error.failed())
        return prefix(std::move(decoded), field_path(name));
    }
    return {};
  }
  return failure(error::unsupported);
}
serialization_diagnostic decode_members(serialization_context& ctx, const registry_ref& registry,
                                        type_id id, void* object, yyjson_val* root,
                                        std::size_t depth,
                                        std::span<const std::string_view> ignored) {
  auto view = mutable_instance::from_object(registry, id, object);
  if (!view)
    return failure(view.error());
  auto properties = view->as_const().properties();
  if (!properties)
    return failure(properties.error());
  if (!yyjson_is_obj(root))
    return failure(error::invalid_argument, "", "根值必须是 JSON 对象");
  auto codec = find_serialization(registry, id);
  if (!codec)
    return failure(codec.error());
  std::unordered_set<std::string_view> names;
  auto it = yyjson_obj_iter_with(root);
  while (auto* key = yyjson_obj_iter_next(&it)) {
    const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
    if (std::ranges::find(ignored, name) != ignored.end())
      continue;
    if (codec->reject_unknown_fields &&
        std::ranges::none_of(*properties, [&](const auto& p) { return p.name == name; }))
      return failure(error::invalid_argument, field_path(name), "未知字段");
    if (!names.insert(name).second)
      return failure(error::invalid_argument, field_path(name), "重复属性名");
  }
  for (const auto& property : *properties) {
    if (std::ranges::find(ignored, property.name) != ignored.end())
      return failure(error::invalid_argument, field_path(property.name), "反射字段占用格式头");
    if (has_flags(property.flags, property_flags::read_only))
      continue;
    auto* input = yyjson_obj_getn(root, property.name.data(), property.name.size());
    const auto path = field_path(property.name);
    if (!input) {
      if (has_flags(property.flags, property_flags::required))
        return failure(error::invalid_argument, path, "缺少必填属性");
      if (property.type.kind() != type_kind::scalar) {
        auto child = view->as_const().property(property.id);
        if (!child)
          return failure(child.error(), path);
        auto status = validate_view(registry, *child, depth + 1);
        if (status.error.failed())
          return prefix(std::move(status), path);
      }
      continue;
    }
    auto child = view->property(property.id);
    if (!child)
      return failure(child.error(), path);
    auto status = has_flags(property.flags, property_flags::enum_name)
                      ? decode_enum_name(*child, input)
                      : decode_view(ctx, registry, *child, input, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), path);
  }
  return {};
}
result<ref<void>, serialization_diagnostic> decode_json(const registry_ref& registry, type_id id,
                                                        const serialization_binding& binding,
                                                        std::span<const std::byte> bytes,
                                                        bool custom = false) {
  if (custom && bytes.size() > binding.maximum_json_bytes)
    return failure(error::invalid_argument, "", "资源描述超过文件大小上限");
  if (!custom && !binding.construct)
    return failure(error::unsupported);
  yyjson_read_err diagnostic{};
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       0, nullptr, &diagnostic),
      &yyjson_doc_free);
  if (!doc)
    return serialization_diagnostic{
        diagnostic.pos, "", diagnostic.msg ? diagnostic.msg : "无效 JSON",
        diagnostic.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? error::out_of_memory
                                                               : error::invalid_argument};
  serialization_context ctx;
  auto* root = yyjson_doc_get_root(doc.get());
  if (custom) {
    detail::json_codec_state state{registry, ctx, 0};
    auto context = state.context();
    return binding.decode_json(context, root);
  }
  auto object = binding.construct();
  if (!object)
    return failure(error::internal);
  auto status = decode_members(ctx, registry, id, object.get(), root, 0);
  if (status.error.failed())
    return status;
  return object;
}
}
result<std::vector<std::byte>, serialization_diagnostic>
serialize_instance(const registry_ref& registry, const instance& view) noexcept try {
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return failure(error::out_of_memory);
  serialization_context ctx;
  auto root = encode_view(ctx, registry, view, doc.get(), 0);
  if (!root)
    return std::move(root).error();
  yyjson_mut_doc_set_root(doc.get(), *root);
  std::size_t size{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &size),
                                                   &std::free);
  if (!text)
    return failure(error::out_of_memory);
  if (view.kind() == type_kind::object) {
    auto binding = find_serialization(registry, view.type().id());
    if (binding && size > binding->maximum_json_bytes)
      return failure(error::invalid_argument, "", "资源描述超过文件大小上限");
  }
  auto bytes = std::as_bytes(std::span(text.get(), size));
  return std::vector<std::byte>(bytes.begin(), bytes.end());
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
serialization_diagnostic deserialize_instance(const registry_ref& registry,
                                              const mutable_instance& view,
                                              std::span<const std::byte> bytes) noexcept try {
  if (view.kind() == type_kind::object) {
    auto binding = find_serialization(registry, view.type().id());
    if (binding && bytes.size() > binding->maximum_json_bytes)
      return failure(error::invalid_argument, "", "资源描述超过文件大小上限");
  }
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0), &yyjson_doc_free);
  if (!doc)
    return failure(error::invalid_argument);
  serialization_context ctx;
  return decode_view(ctx, registry, view, yyjson_doc_get_root(doc.get()), 0);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
result<std::vector<std::byte>, serialization_diagnostic>
serialize_object(const registry_ref& registry, type_id id, const void* object) noexcept try {
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  if (!object)
    return failure(error::invalid_argument);
  if (binding->encode_json)
    return encode_json(registry, id, object, true);
  if (binding->validate) {
    auto status = binding->validate(object);
    if (status.error.failed())
      return status;
  }
  if (binding->encode)
    return binding->encode(registry, object);
  if (binding->reflected_json)
    return encode_json(registry, id, object);
  return failure(error::unsupported);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

result<ref<void>, serialization_diagnostic>
deserialize_object(const registry_ref& registry, type_id id,
                   std::span<const std::byte> bytes) noexcept try {
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  auto decoded = binding->decode_json ? decode_json(registry, id, *binding, bytes, true)
                 : binding->decode    ? binding->decode(registry, bytes)
                 : binding->reflected_json
                     ? decode_json(registry, id, *binding, bytes)
                     : result<ref<void>, serialization_diagnostic>{failure(error::unsupported)};
  if (!decoded)
    return std::move(decoded).error();
  if (!*decoded)
    return failure(error::internal);
  if (binding->validate) {
    auto status = binding->validate(decoded->get());
    if (status.error.failed())
      return status;
  }
  return std::move(*decoded);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

}

namespace gneiss::engine::meta::json {
result<yyjson_mut_val*, serialization_diagnostic> write(yyjson_mut_doc* doc,
                                                        const instance& view) noexcept try {
  if (!doc)
    return failure(error::invalid_argument);
  const auto& registry = detail::property_view_access::registry(view);
  serialization_context ctx;
  return encode_view(ctx, registry, view, doc, 0);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
serialization_diagnostic read(yyjson_val* input, const mutable_instance& view) noexcept try {
  const auto& registry = detail::property_view_access::registry(view);
  serialization_context ctx;
  return decode_view(ctx, registry, view, input, 0);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

}

namespace gneiss::engine::meta {
namespace {
constexpr std::array<std::string_view, 12> value_names{"",        "bool",       "int64",  "uint64",
                                                       "float32", "float64",    "string", "guid",
                                                       "vec3",    "quaternion", "vec4",   "vec2"};
result<value, serialization_diagnostic> read_tagged_value(yyjson_val* input) noexcept try {
  if (!yyjson_is_obj(input) || yyjson_obj_size(input) != 2)
    return failure(error::invalid_argument);
  auto* tag = yyjson_obj_get(input, "kind");
  auto* content = yyjson_obj_get(input, "value");
  if (!yyjson_is_str(tag) || !content)
    return failure(error::invalid_argument);
  const std::string_view name{yyjson_get_str(tag), yyjson_get_len(tag)};
  const auto found = std::ranges::find(value_names, name);
  if (found == value_names.end() || found == value_names.begin())
    return failure(error::unsupported);
  auto decoded =
      read_value(content, static_cast<value_kind>(found - value_names.begin()), "/value");
  if (decoded && !valid_scalar(*decoded))
    return failure(error::invalid_argument, "/value");
  return decoded;
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
result<yyjson_mut_val*, serialization_diagnostic> write_tagged_value(yyjson_mut_doc* doc,
                                                                     const value& input) noexcept
    try {
  if (!doc || !valid_scalar(input))
    return failure(error::invalid_argument);
  auto content = write_value(doc, input, "/value");
  if (!content)
    return std::move(content).error();
  auto* object = yyjson_mut_obj(doc);
  const auto name = value_names[static_cast<std::size_t>(input.kind())];
  if (!object || !yyjson_mut_obj_add_strncpy(doc, object, "kind", name.data(), name.size()) ||
      !yyjson_mut_obj_add_val(doc, object, "value", *content))
    return failure(error::out_of_memory);
  return object;
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

}
error register_builtin_types(const registry_ref& registry) noexcept {
  return type_builder<value>{registry}
      .json_serialization<[](json_codec_context&, yyjson_mut_doc* doc, const value& v) {
        return write_tagged_value(doc, v);
      },
                          [](json_codec_context&, yyjson_val* input) {
                            return read_tagged_value(input);
                          }>()
      .validate<[](const value& v) {
        return valid_scalar(v) ? error::success : error::invalid_argument;
      }>()
      .commit();
}
}

namespace gneiss::engine::meta {
result<std::vector<std::byte>, serialization_diagnostic>
serialize_instance(const instance& view) noexcept {
  return serialize_instance(detail::property_view_access::registry(view), view);
}
serialization_diagnostic deserialize_instance(const mutable_instance& view,
                                              std::span<const std::byte> bytes) noexcept {
  return deserialize_instance(detail::property_view_access::registry(view), view, bytes);
}
}

namespace gneiss::engine::meta {
result<yyjson_mut_val*, serialization_diagnostic>
json_codec_context::write_fields_object(yyjson_mut_doc* doc, type_id id, const void* object) {
  auto& ctx = *state_;
  auto binding = find_serialization(ctx.registry, id);
  if (!binding)
    return failure(binding.error());
  if (!binding->reflected_json)
    return failure(error::unsupported);
  if (!object || !doc)
    return failure(error::invalid_argument);
  if (ctx.depth > 64)
    return failure(error::invalid_argument);
  return encode_members(ctx.traversal, ctx.registry, id, object, doc, ctx.depth);
}
result<ref<void>, serialization_diagnostic>
json_codec_context::read_fields_object(yyjson_val* input, type_id id,
                                       std::span<const std::string_view> ignored) {
  auto& ctx = *state_;
  auto binding = find_serialization(ctx.registry, id);
  if (!binding)
    return failure(binding.error());
  if (!binding->reflected_json || !binding->construct)
    return failure(error::unsupported);
  if (ctx.depth > 64)
    return failure(error::invalid_argument);
  auto object = binding->construct();
  if (!object)
    return failure(error::internal);
  auto status =
      decode_members(ctx.traversal, ctx.registry, id, object.get(), input, ctx.depth, ignored);
  if (status.error.failed())
    return status;
  return object;
}
namespace json {
result<yyjson_mut_val*, serialization_diagnostic> write_fields(yyjson_mut_doc* doc,
                                                               const instance& view) noexcept try {
  auto registry = detail::property_view_access::registry(view);
  serialization_context traversal;
  detail::json_codec_state state{registry, traversal, 0};
  auto ctx = state.context();
  auto address = view.object_data();
  if (!address)
    return failure(address.error());
  auto binding = find_serialization(registry, view.type().id());
  if (!binding)
    return failure(binding.error());
  if (binding->validate) {
    auto status = binding->validate(*address);
    if (status.error.failed())
      return status;
  }
  return detail::json_codec_state::write_fields(ctx, doc, view.type().id(), *address);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
serialization_diagnostic read_fields(yyjson_val* input, const mutable_instance& view,
                                     std::span<const std::string_view> ignored) noexcept try {
  auto registry = detail::property_view_access::registry(view.as_const());
  serialization_context traversal;
  detail::json_codec_state state{registry, traversal, 0};
  auto ctx = state.context();
  auto decoded = detail::json_codec_state::read_fields(ctx, input, view.type().id(), ignored);
  if (!decoded)
    return std::move(decoded).error();
  auto binding = find_serialization(registry, view.type().id());
  if (!binding)
    return failure(binding.error());
  if (binding->validate) {
    auto status = binding->validate(decoded->get());
    if (status.error.failed())
      return status;
  }
  auto status = view.assign_object(view.type().id(), decoded->get());
  return status.failed() ? failure(status) : serialization_diagnostic{};
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

}

}
