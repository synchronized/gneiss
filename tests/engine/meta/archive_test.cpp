// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <cstdio>
#include <deque>
#include <gneiss/engine/core/meta/capabilities/representation.hpp>
#include <gneiss/engine/core/meta/object_type.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/serialization/serialization.hpp>
#include <map>
namespace e = gneiss;
namespace m = e::meta;
namespace s = e::serialization;
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                         \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)
// 仅供测试的内存后端，不包含 JSON 头，也不调用 JSON 编解码。
struct item {
  enum class kind { scalar, object, array, null } type = kind::null;
  e::value data;
  std::vector<std::pair<std::string, item*>> members;
  std::vector<item*> elements;
};
class memory_writer final : public s::writer {
  std::deque<item> nodes_;
  node make(item::kind kind) {
    nodes_.emplace_back();
    nodes_.back().type = kind;
    return &nodes_.back();
  }
  static item* as(node n) { return static_cast<item*>(n); }

public:
  item* root_value{};
  bool fail_members = false;
  node object() override { return make(item::kind::object); }
  node array() override { return make(item::kind::array); }
  node null() override { return make(item::kind::null); }
  node string(std::string_view text) override { return *scalar(e::value{std::string{text}}); }
  e::result<node, gneiss::serialization::diagnostic> scalar(const e::value& v) override {
    auto n = make(item::kind::scalar);
    as(n)->data = v;
    return n;
  }
  bool member(node n, std::string_view key, node v) override {
    if (fail_members || !n || !v)
      return false;
    as(n)->members.emplace_back(key, as(v));
    return true;
  }
  bool append(node n, node v) override {
    if (!n || !v)
      return false;
    as(n)->elements.push_back(as(v));
    return true;
  }
  bool is_object(node n) const override { return n && as(n)->type == item::kind::object; }
  bool is_null(node n) const override { return n && as(n)->type == item::kind::null; }
  bool root(node n) override {
    root_value = as(n);
    return n != nullptr;
  }
};
class memory_reader final : public s::reader {
  const item* root_;
  static const item* as(node n) { return static_cast<const item*>(n); }

public:
  explicit memory_reader(const item* n) : root_(n) {}
  node root() const override { return root_; }
  bool is_object(node n) const override { return n && as(n)->type == item::kind::object; }
  bool is_array(node n) const override { return n && as(n)->type == item::kind::array; }
  bool is_null(node n) const override { return n && as(n)->type == item::kind::null; }
  bool is_string(node n) const override { return n && as(n)->data.kind() == e::value_kind::string; }
  std::string_view string(node n) const override {
    return std::get<std::string>(as(n)->data.payload);
  }
  std::size_t size(node n) const override {
    return is_array(n) ? as(n)->elements.size() : is_object(n) ? as(n)->members.size() : 0;
  }
  node member(node n, std::string_view key) const override {
    if (is_object(n))
      for (const auto& [name, value] : as(n)->members)
        if (name == key)
          return value;
    return nullptr;
  }
  cursor iterate(node n) const override { return {0, size(n), nullptr, n}; }
  node next_element(cursor& c) const override {
    return c.index < c.count ? as(c.owner)->elements[c.index++] : nullptr;
  }
  entry next_member(cursor& c) const override {
    if (c.index >= c.count)
      return {};
    const auto& [name, value] = as(c.owner)->members[c.index++];
    return {name, value};
  }
  e::result<e::value, gneiss::serialization::diagnostic>
  scalar(node n, e::value_kind kind) const override {
    if (!n || as(n)->data.kind() != kind)
      return gneiss::serialization::diagnostic{.error = e::error::invalid_argument};
    return as(n)->data;
  }
};
struct base : m::object {
  static constexpr std::string_view type_name = "archive_base";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 60});
};
struct derived : m::object_type<derived, base> {
  static constexpr std::string_view type_name = "archive_derived";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 61});
  std::int64_t number = 7;
  e::ref<base> next;
};
using choice = std::variant<std::int64_t, std::string>;
namespace gneiss::meta {
template <> struct variant_case_traits<choice> {
  static constexpr std::array<std::string_view, 2> names{"number", "text"};
};
}
struct record {
  static constexpr std::string_view type_name = "archive_record";
  static constexpr e::guid type_guid = e::guid::from_bytes({96, 62});
  std::vector<std::int64_t> numbers;
  std::map<std::string, std::int64_t> names;
  std::optional<std::string> label;
  e::ref<base> object;
  std::int64_t required = 0;
};
bool failed_with(const e::result<void, s::diagnostic>& status, e::error expected) {
  return !status && status.error().error == expected;
}
struct represented_derived : m::object_type<represented_derived, base> {
  static constexpr std::string_view type_name = "represented_derived";
  static constexpr e::guid type_guid = e::guid::literal("b93b8c32-13c0-4ba6-a71d-333e78d54c19");
  std::int64_t number = 7;
};
struct external_id {
  static constexpr std::string_view type_name = "external_id";
  static constexpr e::guid type_guid = e::guid::literal("f530ed20-357b-4943-9eea-d271d71ebbc0");
  std::int64_t number{};
};
struct external_holder {
  static constexpr std::string_view type_name = "external_holder";
  static constexpr e::guid type_guid = e::guid::literal("bd768f30-a7a0-4986-a2d4-fd5508b2db59");
  external_id id{7};
  const external_id& get() const { return id; }
  e::error set(const external_id& candidate) {
    if (candidate.number == 13)
      return e::error::invalid_argument;
    id = candidate;
    return e::error::success;
  }
};
namespace gneiss::meta {
template <> struct representation_traits<represented_derived> {
  static constexpr bool enabled = true;
  using representation_type = std::int64_t;
  static representation_type encode(const represented_derived& source) { return source.number; }
  static represented_derived decode(const representation_type& source) {
    represented_derived output;
    output.number = source;
    return output;
  }
};
template <> struct representation_traits<external_id> {
  static constexpr bool enabled = true;
  using representation_type = std::optional<std::int64_t>;
  static representation_type encode(const external_id& source) {
    return source.number ? representation_type{source.number} : representation_type{};
  }
  static result<external_id> decode(const representation_type& source) {
    if (!source)
      return external_id{};
    if (*source < 0)
      return error::invalid_argument;
    return external_id{*source};
  }
};
}

// 错误适配形成表示环时，由递归预算终止，而不是溢出调用栈。
struct recursive_id {
  std::int64_t number{};
};
namespace gneiss::meta {
template <> struct representation_traits<recursive_id> {
  static constexpr bool enabled = true;
  using representation_type = std::optional<recursive_id>;
  static result<representation_type> encode(const recursive_id& id) {
    if (id.number < 0)
      return error::not_ready;
    return representation_type{id};
  }
  static result<recursive_id> decode(const representation_type& input) {
    return input.value_or(recursive_id{});
  }
};
}

int main() {
  auto r = m::create_registry();
  CHECK(r);
  CHECK(m::registration::type<base>(r)
            .metadata(gneiss::serialization::reference_policy{.polymorphic = true})
            .commit()
            .ok());
  CHECK(m::registration::type<derived>(r)
            .base<base>()
            .property<&derived::number>(m::property_id{1}, "number")
            .property<&derived::next>(m::property_id{2}, "next")
            .commit()
            .ok());
  CHECK(m::registration::type<record>(r)
            .property<&record::numbers>(m::property_id{1}, "numbers")
            .property<&record::names>(m::property_id{2}, "names")
            .property<&record::label>(m::property_id{3}, "label")
            .property<&record::object>(m::property_id{4}, "object")
            .property<&record::required>(m::property_id{5}, "required")(
                gneiss::serialization::field_policy{.required = true})
            .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
            .commit()
            .ok());
  CHECK(gneiss::serialization::register_builtin_types(r).ok());
  CHECK(m::registration::type<represented_derived>(r).base<base>().commit().ok());
  CHECK((m::registration::type<external_holder>(r)
             .property<&external_holder::get, &external_holder::set>(m::property_id{1}, "id")
             .commit()
             .ok()));
  CHECK(m::freeze(r).ok());
  // 自定义类型没有 value 分支；与内置类型共享格式无关的适配机制。
  s::write_context adapted_write{r};
  s::read_context adapted_read{r};
  CHECK(!m::type_of<external_id>(r).registered());
  CHECK(m::type_of<external_id>(r).capability<gneiss::meta::cap::representation>());
  CHECK(!m::type_of<std::int64_t>(r).capability<gneiss::meta::cap::representation>());
  const std::int64_t unrelated = 42;
  auto unrelated_view = m::view(r, unrelated);
  bool consumed = false;
  auto mismatch = m::type_of<external_id>(r).capability<gneiss::meta::cap::representation>().encode(
      unrelated_view, [&](const m::const_instance&) {
        consumed = true;
        return gneiss::error::success;
      });
  CHECK(mismatch == gneiss::error::invalid_argument && !consumed);

  memory_writer adapted_output;
  CHECK(s::write(adapted_write, external_id{42}, adapted_output));
  CHECK(adapted_output.root_value->type == item::kind::scalar);
  CHECK(std::get<std::int64_t>(adapted_output.root_value->data.payload) == 42);
  memory_reader adapted_input{adapted_output.root_value};
  auto adapted_copy = s::decode<external_id>(adapted_read, adapted_input);
  CHECK(adapted_copy && adapted_copy->number == 42);
  s::write_context recursive_write{r, 8};
  s::read_context recursive_read{r, 8};
  memory_writer recursive_output;
  CHECK(failed_with(s::write(recursive_write, recursive_id{-1}, recursive_output),
                    e::error::not_ready));
  CHECK(!s::write(recursive_write, recursive_id{1}, recursive_output));
  CHECK(!s::decode<recursive_id>(recursive_read, adapted_input));

  memory_writer empty_value_output;
  CHECK(s::write(adapted_write, external_id{}, empty_value_output));
  CHECK(empty_value_output.root_value->type == item::kind::null);

  memory_writer defaults_output;
  auto defaults_root = defaults_output.object();
  CHECK(defaults_root);
  CHECK(defaults_output.root(defaults_root));
  memory_reader defaults_input{defaults_output.root_value};
  auto defaults = s::decode<external_holder>(adapted_read, defaults_input);
  CHECK(defaults && defaults->id.number == 7);
  external_holder holder;
  memory_writer holder_output;
  CHECK(s::write(adapted_write, holder, holder_output));
  auto* member = holder_output.root_value->members.front().second;
  memory_reader holder_input{holder_output.root_value};
  auto holder_view = m::view(r, holder);
  member->data = e::value{std::int64_t{-1}};
  CHECK(!s::read_into(adapted_read, holder_input, holder_view));
  CHECK(holder.id.number == 7);
  member->data = e::value{std::int64_t{13}};
  CHECK(!s::read_into(adapted_read, holder_input, holder_view));
  CHECK(holder.id.number == 7);
  member->data = e::value{std::int64_t{21}};
  CHECK(s::read_into(adapted_read, holder_input, holder_view));
  CHECK(holder.id.number == 21);
  s::write_context polymorphic_write{r};
  s::read_context polymorphic_read{r};
  e::ref<base> adapted_object = e::make_ref<represented_derived>();
  memory_writer polymorphic_output;
  CHECK(s::write(polymorphic_write, adapted_object, polymorphic_output));
  memory_reader polymorphic_input{polymorphic_output.root_value};
  auto polymorphic_copy = s::decode<e::ref<base>>(polymorphic_read, polymorphic_input);
  CHECK(polymorphic_copy &&
        dynamic_cast<represented_derived*>(polymorphic_copy->get())->number == 7);

  s::write_context ctx{r};
  s::read_context read_ctx{r};
  CHECK(ctx.property_path().empty());
  static_assert(!std::is_copy_constructible_v<s::write_context>);
  static_assert(!std::is_copy_constructible_v<s::read_context>);
  static_assert(!std::is_constructible_v<s::write_context, s::reference_resolver&>);
  static_assert(!std::is_convertible_v<const s::read_context&, const s::write_context&>);
  static_assert(!std::is_convertible_v<const s::write_context&, const s::read_context&>);
  // 顶层可空值与 JSON 共用类型分派，空值不走对象工厂。
  for (const auto optional : {std::optional<int>{}, std::optional<int>{17}}) {
    memory_writer encoded;
    CHECK(s::write(ctx, optional, encoded).has_value());
    memory_reader decoded{encoded.root_value};
    auto restored = s::decode<std::optional<int>>(read_ctx, decoded);
    CHECK(restored && *restored == optional);
  }
  record original{{1, 2, 3}, {{"a/b~c", 42}}, "hello", e::make_ref<derived>(), 12};
  memory_writer output;
  CHECK(s::write(ctx, original, output).has_value());
  memory_reader input{output.root_value};
  record target_record;
  auto target_view = m::view(r, target_record);
  CHECK(s::read_into(read_ctx, input, target_view));
  CHECK(target_record.required == original.required && target_record.numbers == original.numbers);
  memory_writer borrowed_output;
  CHECK(s::write(target_view.as_const(), borrowed_output));
  memory_reader borrowed_input{borrowed_output.root_value};
  CHECK(s::read_into(borrowed_input, target_view));
  auto copy = s::decode<record>(read_ctx, input);
  CHECK(copy && copy->numbers == original.numbers && copy->names == original.names &&
        copy->label == original.label);
  CHECK(copy->object && copy->object.get() != original.object.get() && copy->required == 12);
  CHECK(dynamic_cast<derived*>(copy->object.get())->number == 7);
  // 后端读错类型后返回相同字段路径，不发布半成品。
  auto required = output.root_value->members.back().second;
  required->data = e::value{std::string{"bad"}};
  auto bad = s::decode<record>(read_ctx, input);
  CHECK(!bad && bad.error().path == "/required" && bad.error().error == e::error::invalid_argument);
  CHECK(read_ctx.property_path().empty());
  required->data = e::value{std::int64_t{12}};
  output.root_value->members.emplace_back("required", required);
  auto duplicate = s::decode<record>(read_ctx, input);
  CHECK(!duplicate && duplicate.error().path == "/required");
  output.root_value->members.pop_back();
  output.root_value->members.back().first = "unknown";
  auto unknown = s::decode<record>(read_ctx, input);
  CHECK(!unknown && unknown.error().path == "/unknown");
  output.root_value->members.pop_back();
  auto missing = s::decode<record>(read_ctx, input);
  CHECK(!missing && missing.error().path == "/required");
  memory_writer failed;
  failed.fail_members = true;
  CHECK(failed_with(s::write(ctx, original, failed), e::error::out_of_memory) &&
        !failed.root_value);
  auto* target = dynamic_cast<derived*>(original.object.get());
  target->next = original.object;
  memory_writer cyclic;
  CHECK(failed_with(s::write(ctx, original, cyclic), e::error::invalid_argument));
  CHECK(ctx.property_path().empty());
  target->next.reset();
  memory_writer recovered;
  CHECK(s::write(ctx, original, recovered).has_value());
  CHECK(ctx.property_path().empty());
  s::write_context shallow{r, 0};
  s::read_context read_shallow{r, 0};
  memory_writer limited;
  CHECK(failed_with(s::write(shallow, original, limited), e::error::invalid_argument));
  memory_reader recovered_input{recovered.root_value};
  CHECK(!s::decode<record>(read_shallow, recovered_input));
  CHECK(s::decode<record>(read_ctx, recovered_input));
  auto other = m::create_registry();
  CHECK(other && m::freeze(other).ok());
  s::write_context mismatched{other};
  s::read_context read_mismatched{other};
  auto view = m::view(r, original);
  CHECK(failed_with(s::write(mismatched, view.as_const(), limited), e::error::invalid_argument));
  CHECK(failed_with(s::read_into(read_mismatched, recovered_input, view),
                    e::error::invalid_argument));
  CHECK(original.required == 12);
  s::write_context invalid{m::registry_ref{}};

  CHECK(failed_with(s::write(invalid, original, limited), e::error::invalid_handle));
  s::read_context read_invalid{m::registry_ref{}};
  CHECK(failed_with(s::read_into(read_invalid, recovered_input, view), e::error::invalid_handle));
  CHECK(read_invalid.property_path().empty());

  original.object.reset();
  original.label.reset();
  memory_writer empty;
  CHECK(s::write(ctx, original, empty).has_value());
  memory_reader empty_input{empty.root_value};
  auto empty_copy = s::decode<record>(read_ctx, empty_input);
  CHECK(empty_copy && !empty_copy->object && !empty_copy->label);
  memory_writer scalar_output;
  CHECK(s::write(ctx, std::int64_t{99}, scalar_output).has_value());
  memory_reader scalar_input{scalar_output.root_value};
  auto scalar = s::decode<std::int64_t>(read_ctx, scalar_input);
  CHECK(scalar && *scalar == 99);
  memory_writer optional_output;
  CHECK(s::write(ctx, std::optional<std::int64_t>{3}, optional_output).has_value());
  memory_reader optional_input{optional_output.root_value};
  auto optional = s::decode<std::optional<std::int64_t>>(read_ctx, optional_input);
  CHECK(optional && *optional && **optional == 3);
  memory_writer variant_output;
  CHECK(s::write(ctx, choice{std::string{"variant"}}, variant_output).has_value());
  memory_reader variant_input{variant_output.root_value};
  auto variant = s::decode<choice>(read_ctx, variant_input);
  CHECK(variant && std::get<std::string>(*variant) == "variant");
  // 通用 variant 遍历在无 JSON 的后端上处理 value 标量和对象内容。
  std::vector<e::value> values{e::value{std::int64_t{42}}, e::value{std::string{"hello"}},
                               e::value{e::ref<m::object>{}}, e::value{e::make_ref<derived>()}};
  memory_writer value_output;
  CHECK(s::write(ctx, values, value_output).has_value());
  memory_reader value_input{value_output.root_value};
  auto value_copy = s::decode<std::vector<e::value>>(read_ctx, value_input);
  CHECK(value_copy && value_copy->size() == values.size());
  CHECK((*value_copy)[0] == values[0] && (*value_copy)[1] == values[1] &&
        (*value_copy)[2] == values[2]);
  auto object_copy = std::get<e::ref<m::object>>((*value_copy)[3].payload);
  auto object_source = std::get<e::ref<m::object>>(values[3].payload);
  CHECK(object_copy && object_copy.get() != object_source.get());
  CHECK(dynamic_cast<derived*>(object_copy.get())->number == 7);
  value_output.root_value->elements[0]->members[0].second->data = e::value{std::string{"unknown"}};
  auto unknown_kind = s::decode<std::vector<e::value>>(read_ctx, value_input);
  CHECK(!unknown_kind && unknown_kind.error().error == e::error::invalid_argument);
  return 0;
}
