// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/serialization/detail/json_adapter.hpp>
#include <gneiss/engine/core/serialization/detail/traversal.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <unordered_set>
#include <yyjson.h>

namespace gneiss::serialization::json {
using namespace meta;
using serialization::detail::context_access;
using serialization::detail::operation_scope;
namespace {
serialization::diagnostic failure(error code, std::string path = {}, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = code};
}
/** 数学值统一逐分量解码；保留具体 C++ 类型及固定长度。 */
template <class T>
result<value, serialization::diagnostic> read_math(yyjson_val* input, const std::string& path) {
  T output{};
  auto components = math::components(output);
  if (!yyjson_is_arr(input) || yyjson_arr_size(input) != components.size())
    return failure(error::invalid_argument, path, "数学值分量数量错误");
  for (std::size_t i = 0; i < components.size(); ++i) {
    auto* item = yyjson_arr_get(input, i);
    if (!yyjson_is_num(item) || !std::isfinite(yyjson_get_num(item)) ||
        std::abs(yyjson_get_num(item)) > std::numeric_limits<float>::max())
      return failure(error::invalid_argument, path + "/" + std::to_string(i),
                     "分量必须为有限 float");
    components[i] = static_cast<float>(yyjson_get_num(item));
  }
  return value{output};
}
result<value, serialization::diagnostic> read_value(yyjson_val* input, value_kind kind,
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
  case value_kind::guid:
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
  case value_kind::matrix3:
    return read_math<math::matrix3>(input, path);
  case value_kind::matrix4:
    return read_math<math::matrix4>(input, path);
  case value_kind::vec2:
    return read_math<math::vector2>(input, path);
  case value_kind::vec3:
    return read_math<math::vector3>(input, path);
  case value_kind::vec4:
    return read_math<math::vector4>(input, path);
  case value_kind::quaternion:
    return read_math<math::quaternion>(input, path);
  default:
    return failure(error::unsupported, std::move(path), "不支持的反射属性类型");
  }
  return failure(error::invalid_argument, std::move(path), "属性 JSON 类型或取值范围错误");
}
result<yyjson_mut_val*, serialization::diagnostic>
write_value(yyjson_mut_doc* doc, const value& input, const std::string& path) {
  return std::visit(
      [&](const auto& item) -> result<yyjson_mut_val*, serialization::diagnostic> {
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
        } else if constexpr (requires { math::components(item); }) {
          output = yyjson_mut_arr(doc);
          if (!output)
            throw std::bad_alloc{};
          for (float number : math::components(item)) {
            if (!std::isfinite(number))
              return failure(error::invalid_argument, path, "不能保存非有限分量");
            if (!yyjson_mut_arr_add_real(doc, output, number))
              throw std::bad_alloc{};
          }
        } else
          return failure(error::unsupported, path);
        if (!output)
          throw std::bad_alloc{};
        return output;
      },
      input.payload);
}

class json_writer final : public serialization::writer {
  yyjson_mut_doc* doc_;
  static yyjson_mut_val* val(node n) { return static_cast<yyjson_mut_val*>(n); }

public:
  explicit json_writer(yyjson_mut_doc* doc) : doc_(doc) {}
  static node allocated(yyjson_mut_val* value) {
    if (!value)
      throw std::bad_alloc{};
    return value;
  }
  node object() override { return allocated(yyjson_mut_obj(doc_)); }
  node array() override { return allocated(yyjson_mut_arr(doc_)); }
  node null() override { return allocated(yyjson_mut_null(doc_)); }
  node string(std::string_view s) override {
    return allocated(yyjson_mut_strncpy(doc_, s.data(), s.size()));
  }
  result<node, serialization::diagnostic> scalar(const value& v) override {
    auto result = write_value(doc_, v, "");
    if (!result)
      return std::move(result).error();
    return static_cast<node>(*result);
  }
  bool member(node o, std::string_view key, node v) override {
    if (!o || !v)
      return false;
    auto name = allocated(yyjson_mut_strncpy(doc_, key.data(), key.size()));
    return yyjson_mut_obj_add(val(o), val(name), val(v));
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
  result<value, serialization::diagnostic> scalar(node n, value_kind k) const override {
    return read_value(val(n), k, "");
  }
};
/** 嵌套 codec 借用同一个环检测上下文，临时 reader 不改变外层输入生命周期。 */
struct scoped_reader {
  const serialization::read_context& context;
  reader* previous;
  json_reader backend;
  explicit scoped_reader(const serialization::read_context& ctx)
      : context(ctx), previous(context_access::input(ctx)) {
    context_access::input(ctx) = &backend;
  }
  ~scoped_reader() { context_access::input(context) = previous; }
};
result<yyjson_mut_val*, serialization::diagnostic>
encode_view(const write_context& ctx, const registry_ref& registry, const const_instance& view,
            yyjson_mut_doc* doc, std::size_t depth) {
  json_writer output{doc};
  auto encoded = serialization::detail::encode_view(ctx, registry, view, &output, depth);
  if (!encoded)
    return std::move(encoded).error();
  return static_cast<yyjson_mut_val*>(*encoded);
}

result<yyjson_mut_val*, serialization::diagnostic>
encode_members(const write_context& ctx, const registry_ref& registry, type_id id,
               const void* object, yyjson_mut_doc* doc, std::size_t depth) {
  json_writer output{doc};
  auto encoded = serialization::detail::encode_members(ctx, registry, id, object, &output, depth);
  if (!encoded)
    return std::move(encoded).error();
  return static_cast<yyjson_mut_val*>(*encoded);
}
serialization::diagnostic decode_view(const read_context& ctx, const registry_ref& registry,
                                      const instance& view, yyjson_val* input, std::size_t depth) {
  scoped_reader scope{ctx};
  return serialization::detail::decode_view(ctx, registry, view, input, depth);
}

serialization::diagnostic decode_members(const read_context& ctx, const registry_ref& registry,
                                         type_id id, void* object, yyjson_val* input,
                                         std::size_t depth,
                                         std::span<const std::string_view> ignored = {}) {
  scoped_reader scope{ctx};
  return serialization::detail::decode_members(ctx, registry, id, object, input, depth, ignored);
}

}
result<std::vector<std::byte>, serialization::diagnostic>
encode_instance(const write_context& ctx, const const_instance& view) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  const auto& registry = *selected;
  if (registry != view.registry())
    return failure(error::invalid_argument);
  operation_scope operation{ctx};
  if (context_access::depth(ctx) > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套深度超限");
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    throw std::bad_alloc{};
  auto root = encode_view(ctx, registry, view, doc.get(), context_access::depth(ctx));
  if (!root)
    return std::move(root).error();
  yyjson_mut_doc_set_root(doc.get(), *root);
  std::size_t size{};
  yyjson_write_err diagnostic{};
  std::unique_ptr<char, decltype(&std::free)> text(
      yyjson_mut_write_opts(doc.get(), 0, nullptr, &size, &diagnostic), &std::free);
  if (!text) {
    if (diagnostic.code == YYJSON_WRITE_ERROR_MEMORY_ALLOCATION)
      throw std::bad_alloc{};
    return failure(error::invalid_argument, "", diagnostic.msg ? diagnostic.msg : "无效 JSON 值");
  }
  auto bytes = std::as_bytes(std::span(text.get(), size));
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
result<void, serialization::diagnostic> read_into(const read_context& ctx, const instance& view,
                                                  std::span<const std::byte> bytes) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  const auto& registry = *selected;
  if (registry != view.registry())
    return failure(error::invalid_argument);
  operation_scope operation{ctx};
  if (context_access::depth(ctx) > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套深度超限");
  yyjson_read_err diagnostic{};
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       0, nullptr, &diagnostic),
      &yyjson_doc_free);
  if (!doc) {
    if (diagnostic.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION)
      throw std::bad_alloc{};
    return serialization::diagnostic{
        diagnostic.pos, "", diagnostic.msg ? diagnostic.msg : "无效 JSON", error::invalid_argument};
  }
  auto status =
      decode_view(ctx, registry, view, yyjson_doc_get_root(doc.get()), context_access::depth(ctx));
  if (status.error.failed())
    return status;
  return {};
}
result<std::vector<std::byte>, serialization::diagnostic>
encode_object(const write_context& ctx, type_id id, const void* object) {
  auto registry = ctx.registry();
  if (!registry)
    return failure(registry.error());
  auto view = const_instance::from_object(*registry, id, object);
  if (!view)
    return failure(view.error());
  return encode_instance(ctx, *view);
}
result<ref<void>, serialization::diagnostic> decode_object(const read_context& ctx, type_id id,
                                                           std::span<const std::byte> bytes) {
  auto registry = ctx.registry();
  if (!registry)
    return failure(registry.error());
  operation_scope operation{ctx};
  yyjson_read_err diagnostic{};
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       0, nullptr, &diagnostic),
      &yyjson_doc_free);
  if (!doc) {
    if (diagnostic.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION)
      throw std::bad_alloc{};
    return serialization::diagnostic{
        diagnostic.pos, "", diagnostic.msg ? diagnostic.msg : "无效 JSON", error::invalid_argument};
  }
  scoped_reader scope{ctx};
  return serialization::detail::decode_owned(ctx, *registry, id, yyjson_doc_get_root(doc.get()),
                                             context_access::depth(ctx), true);
}
}

namespace gneiss::serialization::json::detail {
result<yyjson_mut_val*, serialization::diagnostic>
write(const write_context& ctx, yyjson_mut_doc* doc, const const_instance& view) {
  if (!doc)
    return failure(error::invalid_argument);
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  const auto& registry = *selected;
  if (registry != view.registry())
    return failure(error::invalid_argument);
  operation_scope operation{ctx};
  return encode_view(ctx, registry, view, doc, context_access::depth(ctx));
}
serialization::diagnostic read(const read_context& ctx, yyjson_val* input, const instance& view) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  const auto& registry = *selected;
  if (registry != view.registry())
    return failure(error::invalid_argument);
  operation_scope operation{ctx};
  return decode_view(ctx, registry, view, input, context_access::depth(ctx));
}
}

namespace gneiss::serialization::json {
using namespace meta;
result<std::vector<std::byte>, serialization::diagnostic>
encode_instance(const const_instance& view) {
  write_context ctx{view.registry()};
  return encode_instance(ctx, view);
}
result<void, serialization::diagnostic> read_into(const instance& view,
                                                  std::span<const std::byte> bytes) {
  read_context ctx{view.registry()};
  return read_into(ctx, view, bytes);
}
}

namespace gneiss::serialization::json {
using namespace meta;
namespace detail {
result<yyjson_mut_val*, serialization::diagnostic>
write_fields_object(const write_context& ctx, yyjson_mut_doc* doc, type_id id, const void* object) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  operation_scope operation{ctx, true};
  auto binding = find_policies(*selected, id);
  if (!binding)
    return failure(binding.error());
  if (!object || !doc)
    return failure(error::invalid_argument);
  if (context_access::depth(ctx) > ctx.maximum_depth())
    return failure(error::invalid_argument);
  return encode_members(ctx, *selected, id, object, doc, context_access::depth(ctx));
}
result<ref<void>, serialization::diagnostic>
read_fields_object(const read_context& ctx, yyjson_val* input, type_id id,
                   std::span<const std::string_view> ignored) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  operation_scope operation{ctx, true};
  auto binding = find_policies(*selected, id);
  if (!binding)
    return failure(binding.error());
  auto ops = find_object_ops(*selected, id);
  if (!ops || !ops->construct)
    return failure(error::unsupported);
  if (context_access::depth(ctx) > ctx.maximum_depth())
    return failure(error::invalid_argument);
  auto object = ops->construct();
  if (!object)
    return failure(error::internal);
  auto status =
      decode_members(ctx, *selected, id, object.get(), input, context_access::depth(ctx), ignored);
  if (status.error.failed())
    return status;
  return object;
}
result<yyjson_mut_val*, serialization::diagnostic>
write_value(const write_context& ctx, yyjson_mut_doc* doc, const value& v) {
  auto registry = ctx.registry();
  if (!registry)
    return failure(registry.error());
  auto view = gneiss::meta::view(*registry, std::as_const(v));
  if (!view)
    return failure(view.error());
  return write(ctx, doc, *view);
}
result<value, serialization::diagnostic> read_value(const read_context& ctx, yyjson_val* node) {
  auto registry = ctx.registry();
  if (!registry)
    return failure(registry.error());
  value output;
  auto view = gneiss::meta::view(*registry, output);
  if (!view)
    return failure(view.error());
  auto status = read(ctx, node, *view);
  if (status.error.failed())
    return status;
  return output;
}
result<yyjson_mut_val*, serialization::diagnostic>
write_fields(const write_context& ctx, yyjson_mut_doc* doc, const const_instance& view) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  if (*selected != view.registry())
    return failure(error::invalid_argument);
  auto address = view.object_data();
  if (!address)
    return failure(address.error());
  auto binding = find_policies(*selected, view.type().id());
  if (!binding)
    return failure(binding.error());
  if (binding->validation.validate) {
    auto status = binding->validation.validate(*address);
    if (status.error.failed())
      return status;
  }
  return write_fields_object(ctx, doc, view.type().id(), *address);
}
serialization::diagnostic read_fields(const read_context& ctx, yyjson_val* input,
                                      const instance& view,
                                      std::span<const std::string_view> ignored) {
  auto selected = ctx.registry();
  if (!selected)
    return failure(selected.error());
  if (*selected != view.registry())
    return failure(error::invalid_argument);
  auto decoded = read_fields_object(ctx, input, view.type().id(), ignored);
  if (!decoded)
    return std::move(decoded).error();
  auto binding = find_policies(*selected, view.type().id());
  if (!binding)
    return failure(binding.error());
  if (binding->validation.validate && !(ctx.references() && ctx.references()->collecting())) {
    auto status = binding->validation.validate(decoded->get());
    if (status.error.failed())
      return status;
  }
  return failure(view.assign_object(view.type().id(), decoded->get()));
}
result<yyjson_mut_val*, serialization::diagnostic> write(yyjson_mut_doc* doc,
                                                         const const_instance& view) {
  write_context ctx{view.registry()};
  return write(ctx, doc, view);
}
serialization::diagnostic read(yyjson_val* input, const instance& view) {
  read_context ctx{view.registry()};
  return read(ctx, input, view);
}
result<yyjson_mut_val*, serialization::diagnostic> write_fields(yyjson_mut_doc* doc,
                                                                const const_instance& view) {
  write_context ctx{view.registry()};
  return write_fields(ctx, doc, view);
}
serialization::diagnostic read_fields(yyjson_val* input, const instance& view,
                                      std::span<const std::string_view> ignored) {
  read_context ctx{view.registry()};
  return read_fields(ctx, input, view, ignored);
}
result<yyjson_mut_val*, serialization::diagnostic> write_value(yyjson_mut_doc* doc,
                                                               const value& v) {
  write_context ctx;
  return write_value(ctx, doc, v);
}
result<value, serialization::diagnostic> read_value(yyjson_val* node) {
  read_context ctx;
  return read_value(ctx, node);
}
}

}
