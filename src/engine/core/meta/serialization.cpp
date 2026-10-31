// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/serialization.hpp"
#include "engine/core/meta/instance.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <yyjson.h>

namespace gneiss::engine::meta {
namespace {

struct graph_context {
  struct encoded_object {
    std::string id;
    type_id type;
    ref<const void> owner;
    bool active = true;
  };
  struct decoded_object {
    type_id type;
    ref<void> owner;
    bool active = true;
  };
  std::unordered_map<const void*, encoded_object> encoded;
  std::unordered_map<std::string, decoded_object> decoded;
  yyjson_mut_val* output_objects{};
  yyjson_val* input_objects{};
};
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
          auto text = item.string();
          output = yyjson_mut_strncpy(doc, text.data(), text.size());
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
result<yyjson_mut_val*, serialization_diagnostic> encode_members(graph_context& ctx,
                                                                 const registry_ref&, type_id,
                                                                 const void*, yyjson_mut_doc*,
                                                                 std::size_t);
result<yyjson_mut_val*, serialization_diagnostic>
encode_nested(graph_context& ctx, const registry_ref& registry, type_id id, const void* object,
              yyjson_mut_doc* doc, std::size_t depth) {
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
result<yyjson_mut_val*, serialization_diagnostic>
encode_view(graph_context& ctx, const registry_ref& registry, const instance& view,
            yyjson_mut_doc* doc, std::size_t depth) {
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::reference) {
    auto owned = view.reference_owner();
    if (!owned)
      return failure(owned.error());
    if (!*owned)
      return yyjson_mut_null(doc);
    auto target = value_type.element_type();
    if (!target)
      return failure(target.error());
    if (target->kind() != type_kind::object)
      return failure(error::unsupported, "", "内嵌引用目标必须是注册对象");
    const auto* address = owned->get();
    for (const auto& [other, entry] : ctx.encoded) {
      const bool same_owner = !owned->shared().owner_before(entry.owner.shared()) &&
                              !entry.owner.shared().owner_before(owned->shared());
      if (same_owner != (other == address))
        return failure(error::unsupported, "", "不支持成员别名或重复控制块引用");
    }
    auto found = ctx.encoded.find(address);
    std::string key;
    if (found != ctx.encoded.end()) {
      if (found->second.active)
        return failure(error::invalid_argument, "", "不允许内嵌强引用环");
      if (found->second.type != target->id())
        return failure(error::invalid_argument, "", "同一对象使用了不同静态类型");
      key = found->second.id;
    } else {
      if (ctx.encoded.size() >= 10000 || depth > 64)
        return failure(error::invalid_argument, "", "引用对象数量或深度超限");
      auto binding = find_serialization(registry, target->id());
      if (!binding || !binding->reflected_json || binding->encode || binding->decode ||
          !binding->construct)
        return failure(error::unsupported, "", "内嵌引用目标需要纯反射编解码");
      key = std::to_string(ctx.encoded.size() + 1);
      ctx.encoded.emplace(address, graph_context::encoded_object{key, target->id(), *owned});
      if (binding->validate) {
        auto status = binding->validate(address);
        if (status.error.failed())
          return status;
      }
      auto content = encode_members(ctx, registry, target->id(), address, doc, depth + 1);
      if (!content)
        return std::move(content).error();
      auto* entry = yyjson_mut_obj(doc);
      auto type_guid = target->guid().string();
      if (!entry || !yyjson_mut_obj_add_strcpy(doc, entry, "$type_guid", type_guid.c_str()) ||
          !yyjson_mut_obj_add_val(doc, entry, "$content", *content) ||
          !yyjson_mut_obj_add(ctx.output_objects, yyjson_mut_strcpy(doc, key.c_str()), entry))
        return failure(error::out_of_memory);
      ctx.encoded.at(address).active = false;
    }
    auto* output = yyjson_mut_obj(doc);
    if (!output || !yyjson_mut_obj_add_strcpy(doc, output, "$ref", key.c_str()))
      return failure(error::out_of_memory);
    return output;
  }
  if (depth > 64 && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? write_value(doc, *v, "")
             : result<yyjson_mut_val*, serialization_diagnostic>{failure(v.error())};
  }
  if (value_type.kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return encode_nested(ctx, registry, value_type.id(), *p, doc, depth);
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
    return encode_view(ctx, registry, *child, doc, depth + 1);
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
result<yyjson_mut_val*, serialization_diagnostic>
encode_members(graph_context& ctx, const registry_ref& registry, type_id id, const void* object,
               yyjson_mut_doc* doc, std::size_t depth) {
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
    auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
    if (!encoded)
      return prefix(std::move(encoded).error(), path);
    auto* key = yyjson_mut_strncpy(doc, property.name.data(), property.name.size());
    if (!key || !yyjson_mut_obj_add(root, key, *encoded))
      return failure(error::out_of_memory);
  }
  return root;
}
result<std::vector<std::byte>, serialization_diagnostic>
encode_json(const registry_ref& registry, type_id id, const void* object) {
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return failure(error::out_of_memory);
  graph_context ctx;
  ctx.output_objects = yyjson_mut_obj(doc.get());
  if (!ctx.output_objects)
    return failure(error::out_of_memory);
  auto root = encode_members(ctx, registry, id, object, doc.get(), 0);
  if (!root)
    return std::move(root).error();
  if (!ctx.encoded.empty()) {
    auto* envelope = yyjson_mut_obj(doc.get());
    if (!envelope || !yyjson_mut_obj_add_val(doc.get(), envelope, "$root", *root) ||
        !yyjson_mut_obj_add_val(doc.get(), envelope, "$objects", ctx.output_objects))
      return failure(error::out_of_memory);
    yyjson_mut_doc_set_root(doc.get(), envelope);
  } else
    yyjson_mut_doc_set_root(doc.get(), *root);
  std::size_t size{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &size),
                                                   &std::free);
  if (!text)
    return failure(error::out_of_memory);
  auto bytes = std::as_bytes(std::span{text.get(), size});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
serialization_diagnostic validate_defaults(const registry_ref&, type_id, const void*, std::size_t);
serialization_diagnostic validate_view(const registry_ref& registry, const instance& view,
                                       std::size_t depth) {
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::reference) {
    auto owner = view.reference_owner();
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
serialization_diagnostic decode_members(graph_context& ctx, const registry_ref&, type_id, void*,
                                        yyjson_val*, std::size_t);
serialization_diagnostic decode_nested(graph_context& ctx, const registry_ref& registry,
                                       const mutable_instance& target, yyjson_val* input,
                                       std::size_t depth) {
  if (depth > 64)
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  if (!yyjson_is_obj(input))
    return failure(error::invalid_argument, "", "嵌套值必须是 JSON 对象");
  auto binding = find_serialization(registry, target.type().id());
  if (!binding)
    return failure(binding.error());
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
serialization_diagnostic decode_view(graph_context& ctx, const registry_ref& registry,
                                     const mutable_instance& view, yyjson_val* input,
                                     std::size_t depth) {
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::reference) {
    if (yyjson_is_null(input)) {
      auto status = view.reset_reference();
      return status.failed() ? failure(status) : serialization_diagnostic{};
    }
    auto* ref_id = yyjson_obj_get(input, "$ref");
    if (!yyjson_is_obj(input) || yyjson_obj_size(input) != 1 || !yyjson_is_str(ref_id) ||
        !ctx.input_objects)
      return failure(error::invalid_argument, "", "无效的文件内引用");
    std::string key(yyjson_get_str(ref_id), yyjson_get_len(ref_id));
    auto target = value_type.element_type();
    if (!target || target->kind() != type_kind::object)
      return failure(error::unsupported);
    auto found = ctx.decoded.find(key);
    if (found == ctx.decoded.end()) {
      if (ctx.decoded.size() >= 10000 || depth > 64)
        return failure(error::invalid_argument, "", "引用对象数量或深度超限");
      auto* entry = yyjson_obj_getn(ctx.input_objects, key.data(), key.size());
      auto* type = yyjson_obj_get(entry, "$type_guid");
      auto* content = yyjson_obj_get(entry, "$content");
      if (!yyjson_is_obj(entry) || yyjson_obj_size(entry) != 2 || !yyjson_is_str(type) ||
          !yyjson_is_obj(content))
        return failure(error::invalid_argument, "", "引用 ID 不存在或条目无效");
      auto parsed =
          engine::guid::parse(std::string_view(yyjson_get_str(type), yyjson_get_len(type)));
      if (!parsed || *parsed != target->guid())
        return failure(error::invalid_argument, "", "引用目标类型不匹配");
      auto binding = find_serialization(registry, target->id());
      if (!binding || !binding->reflected_json || binding->encode || binding->decode ||
          !binding->construct)
        return failure(error::unsupported, "", "内嵌引用目标需要纯反射编解码");
      auto object = binding->construct();
      if (!object)
        return failure(error::internal);
      ctx.decoded.emplace(key, graph_context::decoded_object{target->id(), object});
      auto status = decode_members(ctx, registry, target->id(), object.get(), content, depth + 1);
      if (status.error.failed())
        return status;
      if (binding->validate) {
        status = binding->validate(object.get());
        if (status.error.failed())
          return status;
      }
      ctx.decoded.at(key).active = false;
      found = ctx.decoded.find(key);
    }
    if (found->second.active)
      return failure(error::invalid_argument, "", "不允许内嵌强引用环");
    if (found->second.type != target->id())
      return failure(error::invalid_argument, "", "引用目标类型不匹配");
    auto status = view.assign_reference(target->id(), found->second.owner);
    return status.failed() ? failure(status) : serialization_diagnostic{};
  }
  if (depth > 64 && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::object)
    return decode_nested(ctx, registry, view, input, depth);
  if (value_type.kind() == type_kind::scalar) {
    auto decoded = read_value(input, value_type.scalar_kind(), "");
    if (!decoded)
      return std::move(decoded).error();
    auto status = view.write_value(*decoded);
    return status.failed() ? failure(status) : serialization_diagnostic{};
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
serialization_diagnostic decode_members(graph_context& ctx, const registry_ref& registry,
                                        type_id id, void* object, yyjson_val* root,
                                        std::size_t depth) {
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
    if (codec->reject_unknown_fields &&
        std::ranges::none_of(*properties, [&](const auto& p) { return p.name == name; }))
      return failure(error::invalid_argument, field_path(name), "未知字段");
    if (!names.insert(name).second)
      return failure(error::invalid_argument, field_path(name), "重复属性名");
  }
  for (const auto& property : *properties) {
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
    auto status = decode_view(ctx, registry, *child, input, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), path);
  }
  return {};
}
result<ref<void>, serialization_diagnostic> decode_json(const registry_ref& registry, type_id id,
                                                        const serialization_binding& binding,
                                                        std::span<const std::byte> bytes) {
  if (!binding.construct)
    return failure(error::unsupported);
  yyjson_read_err diagnostic{};
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       0, nullptr, &diagnostic),
      &yyjson_doc_free);
  if (!doc)
    return serialization_diagnostic{
        diagnostic.pos, "", diagnostic.msg ? diagnostic.msg : "无效 JSON", error::invalid_argument};
  auto object = binding.construct();
  if (!object)
    return failure(error::internal);
  graph_context ctx;
  auto* root = yyjson_doc_get_root(doc.get());
  if (yyjson_obj_get(root, "$objects")) {
    if (yyjson_obj_size(root) != 2 || !yyjson_is_obj(yyjson_obj_get(root, "$root")))
      return failure(error::invalid_argument, "", "无效的引用文件封装");
    ctx.input_objects = yyjson_obj_get(root, "$objects");
    if (!yyjson_is_obj(ctx.input_objects) || yyjson_obj_size(ctx.input_objects) > 10000)
      return failure(error::invalid_argument, "", "无效的对象表");
    std::unordered_set<std::string> keys;
    auto iter = yyjson_obj_iter_with(ctx.input_objects);
    while (auto* key = yyjson_obj_iter_next(&iter)) {
      if (!keys.emplace(yyjson_get_str(key), yyjson_get_len(key)).second)
        return failure(error::invalid_argument, "", "对象表包含重复 ID");
    }
    root = yyjson_obj_get(root, "$root");
  }
  auto status = decode_members(ctx, registry, id, object.get(), root, 0);
  if (status.error.failed())
    return status;
  if (ctx.input_objects && ctx.decoded.size() != yyjson_obj_size(ctx.input_objects))
    return failure(error::invalid_argument, "", "对象表包含不可达条目");
  return object;
}
}
result<std::vector<std::byte>, serialization_diagnostic>
serialize_object(const registry_ref& registry, type_id id, const void* object) noexcept try {
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  if (!object)
    return failure(error::invalid_argument);
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
  auto decoded = binding->decode ? binding->decode(registry, bytes)
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
result<std::vector<std::byte>, serialization_diagnostic>
serialize_reflected_object(const registry_ref& registry, type_id id, const void* object) noexcept
    try {
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  if (!binding->reflected_json)
    return failure(error::unsupported);
  if (!object)
    return failure(error::invalid_argument);
  if (binding->validate) {
    auto status = binding->validate(object);
    if (status.error.failed())
      return status;
  }
  return encode_json(registry, id, object);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
result<ref<void>, serialization_diagnostic>
deserialize_reflected_object(const registry_ref& registry, type_id id,
                             std::span<const std::byte> bytes) noexcept try {
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  if (!binding->reflected_json)
    return failure(error::unsupported);
  auto decoded = decode_json(registry, id, *binding, bytes);
  if (!decoded)
    return std::move(decoded).error();
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
