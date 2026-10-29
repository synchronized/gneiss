// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/serialization.hpp"
#include "engine/core/meta/instance.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_set>
#include <yyjson.h>

namespace gneiss::engine::meta {
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
    if (yyjson_is_str(input)) {
      const std::string_view text{yyjson_get_str(input), yyjson_get_len(input)};
      auto id = guid::parse(text);
      if (id && id->string() == text) {
        output.payload = *id;
        return output;
      }
    }
    break;
  case value_kind::vec3:
  case value_kind::quaternion: {
    const std::size_t count = kind == value_kind::vec3 ? 3U : 4U;
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
    if (kind == value_kind::vec3)
      output.payload = math::vector3{components[0], components[1], components[2]};
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
result<yyjson_mut_val*, serialization_diagnostic>
encode_members(const registry_ref&, type_id, const void*, yyjson_mut_doc*, std::size_t);
result<yyjson_mut_val*, serialization_diagnostic> encode_nested(const registry_ref& registry,
                                                                type_id id, const void* object,
                                                                yyjson_mut_doc* doc,
                                                                std::size_t depth) {
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
    auto encoded = binding->encode(object);
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
  return encode_members(registry, id, object, doc, depth);
}
result<yyjson_mut_val*, serialization_diagnostic> encode_view(const registry_ref& registry,
                                                              const instance& view,
                                                              yyjson_mut_doc* doc,
                                                              std::size_t depth) {
  if (depth > 64 && view.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (view.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? write_value(doc, *v, "")
             : result<yyjson_mut_val*, serialization_diagnostic>{failure(v.error())};
  }
  if (view.kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return encode_nested(registry, view.type_id(), *p, doc, depth);
  }
  if (view.kind() == type_kind::optional) {
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
    return encode_view(registry, *child, doc, depth + 1);
  }
  if (view.kind() == type_kind::sequence) {
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
      auto encoded = encode_view(registry, *child, doc, depth + 1);
      if (!encoded)
        return prefix(std::move(encoded).error(), "/" + std::to_string(i));
      if (!yyjson_mut_arr_append(output, *encoded))
        return failure(error::out_of_memory);
    }
    return output;
  }
  if (view.kind() == type_kind::associative) {
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
      auto encoded = encode_view(registry, *child, doc, depth + 1);
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
result<yyjson_mut_val*, serialization_diagnostic> encode_members(const registry_ref& registry,
                                                                 type_id id, const void* object,
                                                                 yyjson_mut_doc* doc,
                                                                 std::size_t depth) {
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
    auto encoded = encode_view(registry, *child, doc, depth + 1);
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
  auto root = encode_members(registry, id, object, doc.get(), 0);
  if (!root)
    return std::move(root).error();
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
  if (depth > 64 && view.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (view.kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return validate_defaults(registry, view.type_id(), *p, depth);
  }
  if (view.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? serialization_diagnostic{} : failure(v.error());
  }
  if (view.kind() == type_kind::optional) {
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
  if (view.kind() == type_kind::sequence) {
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
    if (!property.object_type || has_flags(property.flags, property_flags::read_only))
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
serialization_diagnostic decode_members(const registry_ref&, type_id, void*, yyjson_val*,
                                        std::size_t);
serialization_diagnostic decode_nested(const registry_ref& registry, const mutable_instance& target,
                                       yyjson_val* input, std::size_t depth) {
  if (depth > 64)
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  if (!yyjson_is_obj(input))
    return failure(error::invalid_argument, "", "嵌套值必须是 JSON 对象");
  auto binding = find_serialization(registry, target.as_const().type_id());
  if (!binding)
    return failure(binding.error());
  if (binding->decode) {
    std::size_t size{};
    std::unique_ptr<char, decltype(&std::free)> text(yyjson_val_write(input, 0, &size), &std::free);
    if (!text)
      return failure(error::out_of_memory);
    auto value = binding->decode(std::as_bytes(std::span{text.get(), size}));
    if (!value)
      return std::move(value).error();
    if (!*value)
      return failure(error::internal);
    if (binding->validate) {
      auto status = binding->validate(value->get());
      if (status.error.failed())
        return status;
    }
    const auto assigned = target.assign_object(target.as_const().type_id(), value->get());
    return assigned.failed() ? failure(assigned) : serialization_diagnostic{};
  }
  if (!binding->reflected_json)
    return failure(error::unsupported);
  auto address = target.object_data();
  if (!address)
    return failure(address.error());
  auto* object = *address;
  auto status = decode_members(registry, target.as_const().type_id(), object, input, depth);
  if (status.error.failed())
    return status;
  return binding->validate ? binding->validate(object) : serialization_diagnostic{};
}
serialization_diagnostic decode_view(const registry_ref& registry, const mutable_instance& view,
                                     yyjson_val* input, std::size_t depth) {
  if (depth > 64 && view.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (view.kind() == type_kind::object)
    return decode_nested(registry, view, input, depth);
  if (view.kind() == type_kind::scalar) {
    auto decoded = read_value(input, view.as_const().scalar_kind(), "");
    if (!decoded)
      return std::move(decoded).error();
    auto status = view.write_value(*decoded);
    return status.failed() ? failure(status) : serialization_diagnostic{};
  }
  if (view.kind() == type_kind::optional) {
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
    return decode_view(registry, *child, input, depth + 1);
  }
  if (view.kind() == type_kind::sequence) {
    if (!yyjson_is_arr(input))
      return failure(error::invalid_argument, "", "序列必须是 JSON 数组");
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    auto status = v->resize(0);
    if (status.failed())
      return failure(status);
    status = v->resize(yyjson_arr_size(input));
    if (status.failed())
      return failure(status);
    std::size_t i = 0;
    auto it = yyjson_arr_iter_with(input);
    while (auto* item = yyjson_arr_iter_next(&it)) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      auto decoded = decode_view(registry, *child, item, depth + 1);
      if (decoded.error.failed())
        return prefix(std::move(decoded), "/" + std::to_string(i));
      ++i;
    }
    return {};
  }
  if (view.kind() == type_kind::associative) {
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
      auto decoded = decode_view(registry, *child, yyjson_obj_iter_get_val(key), depth + 1);
      if (decoded.error.failed())
        return prefix(std::move(decoded), field_path(name));
    }
    return {};
  }
  return failure(error::unsupported);
}
serialization_diagnostic decode_members(const registry_ref& registry, type_id id, void* object,
                                        yyjson_val* root, std::size_t depth) {
  auto view = mutable_instance::from_object(registry, id, object);
  if (!view)
    return failure(view.error());
  auto properties = view->as_const().properties();
  if (!properties)
    return failure(properties.error());
  if (!yyjson_is_obj(root))
    return failure(error::invalid_argument, "", "根值必须是 JSON 对象");
  std::unordered_set<std::string_view> names;
  auto it = yyjson_obj_iter_with(root);
  while (auto* key = yyjson_obj_iter_next(&it)) {
    const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
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
      if (property.object_type) {
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
    auto status = decode_view(registry, *child, input, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), path);
  }
  return {};
}
result<std::shared_ptr<void>, serialization_diagnostic>
decode_json(const registry_ref& registry, type_id id, const serialization_binding& binding,
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
  auto status = decode_members(registry, id, object.get(), yyjson_doc_get_root(doc.get()), 0);
  if (status.error.failed())
    return status;
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
    return binding->encode(object);
  if (binding->reflected_json)
    return encode_json(registry, id, object);
  return failure(error::unsupported);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

result<std::shared_ptr<void>, serialization_diagnostic>
deserialize_object(const registry_ref& registry, type_id id,
                   std::span<const std::byte> bytes) noexcept try {
  auto binding = find_serialization(registry, id);
  if (!binding)
    return failure(binding.error());
  auto decoded =
      binding->decode ? binding->decode(bytes)
      : binding->reflected_json
          ? decode_json(registry, id, *binding, bytes)
          : result<std::shared_ptr<void>, serialization_diagnostic>{failure(error::unsupported)};
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
