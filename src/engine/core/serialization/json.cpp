// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/serialization/json.hpp"
#include "engine/core/meta/detail/property_view_access.hpp"
#include "engine/core/meta/detail/reference_view_access.hpp"
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/detail/json_adapter.hpp"
#include "engine/core/serialization/detail/traversal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_set>
#include <yyjson.h>

namespace gneiss::engine::serialization::json {
using namespace meta;
namespace {
using serialization_context = serialization::detail::traversal_context;
}
namespace detail {
struct json_codec_state {
  const registry_ref& registry;
  serialization_context& traversal;
  std::size_t depth;
  json_codec_context context() { return json_codec_context{*this}; }
  static json_codec_state& get(json_codec_context& context) { return *context.state_; }
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
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, ref<meta::object>>)
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

class json_writer final : public serialization::writer {
  yyjson_mut_doc* doc_;
  static yyjson_mut_val* val(node n) { return static_cast<yyjson_mut_val*>(n); }

public:
  explicit json_writer(yyjson_mut_doc* doc) : doc_(doc) {}
  node object() override { return yyjson_mut_obj(doc_); }
  node array() override { return yyjson_mut_arr(doc_); }
  node null() override { return yyjson_mut_null(doc_); }
  node string(std::string_view s) override { return yyjson_mut_strncpy(doc_, s.data(), s.size()); }
  result<node, serialization_diagnostic> scalar(const value& v) override {
    auto result = write_value(doc_, v, "");
    if (!result)
      return std::move(result).error();
    return static_cast<node>(*result);
  }
  bool member(node o, std::string_view key, node v) override {
    if (!o || !v)
      return false;
    auto name = yyjson_mut_strncpy(doc_, key.data(), key.size());
    return name && yyjson_mut_obj_add(val(o), name, val(v));
  }
  bool append(node a, node v) override { return a && v && yyjson_mut_arr_append(val(a), val(v)); }
  bool is_object(node n) const override { return yyjson_mut_is_obj(val(n)); }
  bool is_null(node n) const override { return yyjson_mut_is_null(val(n)); }
  bool root(node n) override {
    if (!n)
      return false;
    yyjson_mut_doc_set_root(doc_, val(n));
    return true;
  }
  bool has_custom(const serialization_binding& b) const override {
    return b.json.encode_node || b.json.encode_bytes;
  }
  result<node, serialization_diagnostic> custom(serialization_context& ctx,
                                                const registry_ref& registry, type_id,
                                                const serialization_binding& binding,
                                                const void* object, std::size_t depth) override {
    if (binding.json.encode_node) {
      detail::json_codec_state state{registry, ctx, depth};
      auto context = state.context();
      auto encoded = binding.json.encode_node(context, doc_, object);
      if (!encoded)
        return std::move(encoded).error();
      return static_cast<node>(*encoded);
    }
    auto encoded = binding.json.encode_bytes(registry, object);
    if (!encoded)
      return std::move(encoded).error();
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> parsed(
        yyjson_read(reinterpret_cast<const char*>(encoded->data()), encoded->size(), 0),
        &yyjson_doc_free);
    if (!parsed || !yyjson_is_obj(yyjson_doc_get_root(parsed.get())))
      return failure(error::invalid_argument, "", "嵌套 codec 必须输出 JSON 对象");
    auto copied = yyjson_val_mut_copy(doc_, yyjson_doc_get_root(parsed.get()));
    if (!copied)
      return failure(error::out_of_memory);
    return static_cast<node>(copied);
  }
};
class json_reader final : public serialization::reader {
  yyjson_val* root_{};
  static yyjson_val* val(node n) {
    return const_cast<yyjson_val*>(static_cast<const yyjson_val*>(n));
  }

public:
  explicit json_reader(yyjson_val* root = nullptr) : root_(root) {}
  node root() const override { return root_; }
  bool is_object(node n) const override { return yyjson_is_obj(val(n)); }
  bool is_array(node n) const override { return yyjson_is_arr(val(n)); }
  bool is_null(node n) const override { return yyjson_is_null(val(n)); }
  bool is_string(node n) const override { return yyjson_is_str(val(n)); }
  std::string_view string(node n) const override {
    return {yyjson_get_str(val(n)), yyjson_get_len(val(n))};
  }
  std::size_t size(node n) const override {
    return is_array(n) ? yyjson_arr_size(val(n)) : yyjson_obj_size(val(n));
  }
  node member(node n, std::string_view key) const override {
    return yyjson_obj_getn(val(n), key.data(), key.size());
  }
  cursor iterate(node n) const override {
    if (is_array(n)) {
      auto i = yyjson_arr_iter_with(val(n));
      return {i.idx, i.max, i.cur, n};
    }
    auto i = yyjson_obj_iter_with(val(n));
    return {i.idx, i.max, i.cur, n};
  }
  node next_element(cursor& c) const override {
    yyjson_arr_iter i{c.index, c.count, val(c.current)};
    auto result = yyjson_arr_iter_next(&i);
    c.index = i.idx;
    c.current = i.cur;
    return result;
  }
  entry next_member(cursor& c) const override {
    yyjson_obj_iter i{c.index, c.count, val(c.current), val(c.owner)};
    auto key = yyjson_obj_iter_next(&i);
    c.index = i.idx;
    c.current = i.cur;
    if (!key)
      return {};
    return {{yyjson_get_str(key), yyjson_get_len(key)}, yyjson_obj_iter_get_val(key)};
  }
  result<value, serialization_diagnostic> scalar(node n, value_kind k) const override {
    return read_value(val(n), k, "");
  }
  bool has_custom(const serialization_binding& b) const override {
    return b.json.decode_node || b.json.decode_bytes;
  }
  result<ref<void>, serialization_diagnostic> custom(serialization_context& ctx,
                                                     const registry_ref& registry, type_id,
                                                     const serialization_binding& binding,
                                                     node input, std::size_t depth) override {
    if (binding.json.decode_node) {
      detail::json_codec_state state{registry, ctx, depth};
      auto context = state.context();
      return binding.json.decode_node(context, val(input));
    }
    std::size_t size{};
    std::unique_ptr<char, decltype(&std::free)> text(yyjson_val_write(val(input), 0, &size),
                                                     &std::free);
    if (!text)
      return failure(error::out_of_memory);
    return binding.json.decode_bytes(registry, std::as_bytes(std::span{text.get(), size}));
  }
};
/** 嵌套 codec 借用同一个环检测上下文，临时 reader 不改变外层输入生命周期。 */
struct scoped_reader {
  serialization_context& context;
  reader* previous;
  json_reader backend;
  explicit scoped_reader(serialization_context& ctx) : context(ctx), previous(ctx.input) {
    ctx.input = &backend;
  }
  ~scoped_reader() { context.input = previous; }
};
result<yyjson_mut_val*, serialization_diagnostic>
encode_view(serialization_context& ctx, const registry_ref& registry, const instance& view,
            yyjson_mut_doc* doc, std::size_t depth) {
  json_writer output{doc};
  auto encoded = serialization::detail::encode_view(ctx, registry, view, &output, depth);
  if (!encoded)
    return std::move(encoded).error();
  return static_cast<yyjson_mut_val*>(*encoded);
}
result<yyjson_mut_val*, serialization_diagnostic>
encode_nested(serialization_context& ctx, const registry_ref& registry, type_id id,
              const void* object, yyjson_mut_doc* doc, std::size_t depth) {
  json_writer output{doc};
  auto encoded = serialization::detail::encode_nested(ctx, registry, id, object, &output, depth);
  if (!encoded)
    return std::move(encoded).error();
  return static_cast<yyjson_mut_val*>(*encoded);
}
result<yyjson_mut_val*, serialization_diagnostic>
encode_members(serialization_context& ctx, const registry_ref& registry, type_id id,
               const void* object, yyjson_mut_doc* doc, std::size_t depth) {
  json_writer output{doc};
  auto encoded = serialization::detail::encode_members(ctx, registry, id, object, &output, depth);
  if (!encoded)
    return std::move(encoded).error();
  return static_cast<yyjson_mut_val*>(*encoded);
}
serialization_diagnostic decode_view(serialization_context& ctx, const registry_ref& registry,
                                     const mutable_instance& view, yyjson_val* input,
                                     std::size_t depth) {
  scoped_reader scope{ctx};
  return serialization::detail::decode_view(ctx, registry, view, input, depth);
}
result<ref<void>, serialization_diagnostic> decode_owned(serialization_context& ctx,
                                                         const registry_ref& registry, type_id id,
                                                         yyjson_val* input, std::size_t depth) {
  scoped_reader scope{ctx};
  return serialization::detail::decode_owned(ctx, registry, id, input, depth);
}
serialization_diagnostic decode_members(serialization_context& ctx, const registry_ref& registry,
                                        type_id id, void* object, yyjson_val* input,
                                        std::size_t depth,
                                        std::span<const std::string_view> ignored = {}) {
  scoped_reader scope{ctx};
  return serialization::detail::decode_members(ctx, registry, id, object, input, depth, ignored);
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
    if (binding && size > binding->json.maximum_bytes)
      return failure(error::invalid_argument, "", "资源描述超过文件大小上限");
  }
  auto bytes = std::as_bytes(std::span{text.get(), size});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
result<ref<void>, serialization_diagnostic> decode_json(const registry_ref& registry, type_id id,
                                                        const serialization_binding& binding,
                                                        std::span<const std::byte> bytes,
                                                        bool custom = false) {
  if (custom && bytes.size() > binding.json.maximum_bytes)
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
    return binding.json.decode_node(context, root);
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
    if (binding && size > binding->json.maximum_bytes)
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
    if (binding && bytes.size() > binding->json.maximum_bytes)
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
  if (binding->json.encode_node)
    return encode_json(registry, id, object, true);
  if (binding->validate) {
    auto status = binding->validate(object);
    if (status.error.failed())
      return status;
  }
  if (binding->json.encode_bytes)
    return binding->json.encode_bytes(registry, object);
  if (binding->reflected)
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
  auto decoded = binding->json.decode_node    ? decode_json(registry, id, *binding, bytes, true)
                 : binding->json.decode_bytes ? binding->json.decode_bytes(registry, bytes)
                 : binding->reflected
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

namespace gneiss::engine::serialization::json {
result<yyjson_mut_val*, serialization_diagnostic> write(yyjson_mut_doc* doc,
                                                        const instance& view) noexcept try {
  if (!doc)
    return failure(error::invalid_argument);
  const auto& registry = meta::detail::property_view_access::registry(view);
  serialization_context ctx;
  return encode_view(ctx, registry, view, doc, 0);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
serialization_diagnostic read(yyjson_val* input, const mutable_instance& view) noexcept try {
  const auto& registry = meta::detail::property_view_access::registry(view);
  serialization_context ctx;
  return decode_view(ctx, registry, view, input, 0);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

}

namespace gneiss::engine::serialization::json {
using namespace meta;
namespace {
constexpr std::array<std::string_view, 13> value_names{
    "",     "bool", "int64",      "uint64", "float32", "float64", "string",
    "guid", "vec3", "quaternion", "vec4",   "vec2",    "object"};
result<value, serialization_diagnostic> read_tagged_value(json_codec_context& context,
                                                          yyjson_val* input) noexcept try {
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
  if (name == "object") {
    if (yyjson_is_null(content))
      return value{ref<meta::object>{}};
    auto* tag = yyjson_obj_get(content, "$type_guid");
    auto* fields = yyjson_obj_get(content, "$content");
    if (!yyjson_is_obj(content) || yyjson_obj_size(content) != 2 || !yyjson_is_str(tag) ||
        !yyjson_is_obj(fields))
      return failure(error::invalid_argument, "/value", "对象需要类型和内容");
    auto guid = engine::guid::parse({yyjson_get_str(tag), yyjson_get_len(tag)});
    if (!guid || !*guid ||
        guid->string() != std::string_view{yyjson_get_str(tag), yyjson_get_len(tag)})
      return failure(error::invalid_argument, "/value/$type_guid");
    auto& state = detail::json_codec_state::get(context);
    auto type = describe_type(state.registry, *guid);
    if (!type)
      return failure(type.error(), "/value/$type_guid");
    auto binding = find_serialization(state.registry, type->type_id);
    if (!binding || !binding->as_object)
      return failure(error::unsupported, "/value", "类型不是可反射对象");
    auto decoded =
        decode_owned(state.traversal, state.registry, type->type_id, fields, state.depth);
    if (!decoded)
      return prefix(std::move(decoded).error(), "/value/$content");
    auto* object = binding->as_object(decoded->get());
    if (!object || object->dynamic_type() != type->type_id ||
        object->dynamic_guid() != type->guid || object->dynamic_address() != decoded->get())
      return failure(error::invalid_argument, "/value", "对象实际身份与注册类型不符");
    return value{decoded->alias(object)};
  }
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
result<yyjson_mut_val*, serialization_diagnostic>
write_tagged_value(json_codec_context& context, yyjson_mut_doc* doc, const value& input) noexcept
    try {
  if (!doc || (input.kind() != value_kind::object && !valid_scalar(input)))
    return failure(error::invalid_argument);
  auto content = [&]() -> result<yyjson_mut_val*, serialization_diagnostic> {
    if (input.kind() != value_kind::object)
      return write_value(doc, input, "/value");
    const auto& owner = std::get<ref<meta::object>>(input.payload);
    if (!owner)
      return yyjson_mut_null(doc);
    auto& state = detail::json_codec_state::get(context);
    auto type = describe_type(state.registry, owner->dynamic_type());
    if (!type)
      return failure(type.error(), "/value/$type_guid");
    auto binding = find_serialization(state.registry, owner->dynamic_type());
    if (!binding || !binding->as_object || type->guid != owner->dynamic_guid())
      return failure(error::unsupported, "/value");
    const auto* address = owner->dynamic_address();
    if (state.depth > 64 || !state.traversal.active.insert(address).second)
      return failure(error::invalid_argument, "/value", "引用环或嵌套深度超限");
    auto fields = encode_nested(state.traversal, state.registry, owner->dynamic_type(), address,
                                doc, state.depth + 1);
    state.traversal.active.erase(address);
    if (!fields)
      return prefix(std::move(fields).error(), "/value/$content");
    auto* envelope = yyjson_mut_obj(doc);
    const auto guid = type->guid.string();
    if (!envelope || !yyjson_mut_obj_add_strcpy(doc, envelope, "$type_guid", guid.c_str()) ||
        !yyjson_mut_obj_add_val(doc, envelope, "$content", *fields))
      return failure(error::out_of_memory);
    return envelope;
  }();
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
error register_value_type(const registry_ref& registry) noexcept {
  return type_builder<value>{registry}
      .json_serialization<[](json_codec_context& ctx, yyjson_mut_doc* doc, const value& v) {
        return write_tagged_value(ctx, doc, v);
      },
                          [](json_codec_context& ctx, yyjson_val* input) {
                            return read_tagged_value(ctx, input);
                          }>()
      .validate<[](const value& v) {
        return v.kind() == value_kind::object || valid_scalar(v) ? error::success
                                                                 : error::invalid_argument;
      }>()
      .commit();
}
}

namespace gneiss::engine::serialization::json {
using namespace meta;
result<std::vector<std::byte>, serialization_diagnostic>
serialize_instance(const instance& view) noexcept {
  return serialize_instance(meta::detail::property_view_access::registry(view), view);
}
serialization_diagnostic deserialize_instance(const mutable_instance& view,
                                              std::span<const std::byte> bytes) noexcept {
  return deserialize_instance(meta::detail::property_view_access::registry(view), view, bytes);
}
}

namespace gneiss::engine::serialization::json {
using namespace meta;
result<yyjson_mut_val*, serialization_diagnostic>
json_codec_context::write_fields_object(yyjson_mut_doc* doc, type_id id, const void* object) {
  auto& ctx = *state_;
  auto binding = find_serialization(ctx.registry, id);
  if (!binding)
    return failure(binding.error());
  if (!binding->reflected)
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
  if (!binding->reflected || !binding->construct)
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

result<yyjson_mut_val*, serialization_diagnostic> write_value(yyjson_mut_doc* doc,
                                                              const value& v) noexcept try {
  auto registry = default_registry();
  if (!registry)
    return failure(registry.error());
  serialization_context traversal;
  detail::json_codec_state state{*registry, traversal, 0};
  auto context = state.context();
  return write_tagged_value(context, doc, v);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}
result<value, serialization_diagnostic> read_value(yyjson_val* node) noexcept try {
  auto registry = default_registry();
  if (!registry)
    return failure(registry.error());
  serialization_context traversal;
  detail::json_codec_state state{*registry, traversal, 0};
  auto context = state.context();
  return read_tagged_value(context, node);
} catch (const std::bad_alloc&) {
  return failure(error::out_of_memory);
} catch (...) {
  return failure(error::internal);
}

result<yyjson_mut_val*, serialization_diagnostic> write_fields(yyjson_mut_doc* doc,
                                                               const instance& view) noexcept try {
  auto registry = meta::detail::property_view_access::registry(view);
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
  auto registry = meta::detail::property_view_access::registry(view.as_const());
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

namespace gneiss::engine::meta {
error register_builtin_types(const registry_ref& registry) noexcept {
  return serialization::json::register_value_type(registry);
}
}
