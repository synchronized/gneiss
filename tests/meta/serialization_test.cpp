// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/object_type.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/serialization/serialization.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/core/value_edit.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/material/material_description.hpp"
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
namespace e = gneiss::engine;
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
struct probe {
  inline static constexpr std::string_view type_name = "codec_probe";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({7, 6, 5})};
  int number = 3;
};
e::result<void*, e::serialization::diagnostic> bad_encode(const e::serialization::context&,
                                                          e::serialization::writer&, const probe&) {
  throw std::bad_alloc{};
}
e::result<probe, e::serialization::diagnostic> bad_decode(const e::serialization::context&,
                                                          e::serialization::reader&, const void*) {
  return e::serialization::diagnostic{.error = e::error::invalid_argument};
}
struct move_only {
  inline static constexpr std::string_view type_name = "move_only_codec";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({7, 6, 6})};
  std::unique_ptr<int> number;
  explicit move_only(int value) : number(std::make_unique<int>(value)) {}
  move_only(move_only&&) noexcept = default;
  move_only& operator=(move_only&&) = delete;
};
e::result<move_only, e::serialization::diagnostic>
decode_move_only(const e::serialization::context&, e::serialization::reader& input,
                 const void* node) {
  auto number = input.member(node, "number");
  if (!number)
    return e::serialization::diagnostic{3, "/number", "缺少数据", e::error::invalid_argument};
  auto decoded = input.scalar(number, e::value_kind::int64);
  if (!decoded)
    return std::move(decoded).error();
  return move_only{static_cast<int>(std::get<std::int64_t>(decoded->payload))};
}
struct captured_data {
  static constexpr std::string_view type_name = "captured_data";
  static constexpr e::guid type_guid = e::guid::literal("05db59e0-b757-40d8-b719-295b8f961e08");
  std::vector<std::optional<int>> values;
  std::map<std::string, std::vector<int>> groups;
  bool operator==(const captured_data&) const = default;
};
struct cyclic_data : e::meta::object_type<cyclic_data> {
  static constexpr std::string_view type_name = "cyclic_data";
  static constexpr e::guid type_guid = e::guid::literal("7ee666e2-3216-43a0-80ca-c6110da175fd");
  e::ref<cyclic_data> next;
};
// 两层继承且反射基类不是首个基类，验证访问不能假定地址不变。
struct inherited_base : e::meta::object_type<inherited_base> {
  static constexpr std::string_view type_name = "inherited_base";
  static constexpr e::guid type_guid = e::guid::literal("f88fb857-aad7-4310-b537-48ea7dca3505");
  int number = 4;
  std::vector<int> values{1, 2};
  captured_data nested;
  const captured_data& get_nested() const { return nested; }
  e::error set_nested(const captured_data& candidate) {
    if (candidate.values.empty())
      return e::error::invalid_argument;
    nested = candidate;
    return e::error::success;
  }
};
struct inherited_middle : e::meta::object_type<inherited_middle, inherited_base> {
  static constexpr std::string_view type_name = "inherited_middle";
  static constexpr e::guid type_guid = e::guid::literal("6e7cddc3-aa9a-4e92-a23b-e45c777f4994");
  int middle = 5;
};
struct unrelated_base {
  virtual ~unrelated_base() = default;
  std::uint64_t sentinel = 123;
};
struct inherited_leaf : unrelated_base, e::meta::object_type<inherited_leaf, inherited_middle> {
  static constexpr std::string_view type_name = "inherited_leaf";
  static constexpr e::guid type_guid = e::guid::literal("3d36e83d-41b0-4e49-81f9-772489155c85");
  int leaf = 6;
};
bool inherited_properties() {
  using namespace e::meta;
  auto created = create_registry();
  if (!created)
    return false;
  auto registry = *created;
  if (registration<captured_data>{registry}
          .property<&captured_data::values>(property_id{1}, "values")
          .property<&captured_data::groups>(property_id{2}, "groups")
          .commit()
          .failed())
    return false;
  // 先注册派生类；冻结缺少基类时失败，补齐后可以重试。
  if (registration<inherited_leaf>{registry}
          .base<inherited_middle>()
          .property<&inherited_leaf::leaf>(property_id{4}, "leaf")
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit()
          .failed() ||
      freeze(registry) != e::error::not_found ||
      registration<inherited_middle>{registry}
          .base<inherited_base>()
          .property<&inherited_middle::middle>(property_id{3}, "middle")
          .commit()
          .failed() ||
      registration<inherited_base>{registry}
          .metadata(gneiss::engine::serialization::reference_policy{.polymorphic = true})
          .property<&inherited_base::number>(property_id{1}, "number")
          .property_metadata(property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&inherited_base::values>(property_id{2}, "values")
          .property<&inherited_base::get_nested, &inherited_base::set_nested>(property_id{5},
                                                                              "nested")
          .commit()
          .failed() ||
      freeze(registry).failed() || freeze(registry).failed())
    return false;
  inherited_leaf input;
  if (static_cast<void*>(&input) == static_cast<void*>(static_cast<inherited_base*>(&input)))
    return false;
  auto view = gneiss::engine::meta::view(registry, input);
  auto properties = find_type(registry, e::runtime_type_of<inherited_leaf>());
  if (!view || !properties || properties.properties().size() != 5)
    return false;
  auto number = view->member("number");
  auto values = view->member("values");
  if (!number || !values || number->assign(e::value{std::int64_t{42}}).failed() ||
      input.number != 42 || input.sentinel != 123 || !values->as_sequence())
    return false;
  auto read = read_property(registry, inherited_leaf::type_guid, property_id{1}, &input);
  if (!read || *read != e::value{std::int64_t{42}} ||
      write_property(registry, inherited_leaf::type_guid, property_id{1}, &input,
                     e::value{std::int64_t{43}})
          .failed() ||
      input.number != 43)
    return false;
  auto sequence = values->as_sequence();
  if (!sequence || sequence->resize(3).failed())
    return false;
  auto element = sequence->at(2);
  if (!element || element->assign(e::value{std::int64_t{9}}).failed() || input.values[2] != 9)
    return false;
  auto nested = view->member("nested");
  captured_data candidate{{7}, {{"entry", {8}}}};
  auto candidate_view = gneiss::engine::meta::view(registry, std::as_const(candidate));
  if (!nested || !candidate_view || nested->assign(*candidate_view).failed() ||
      input.nested != candidate)
    return false;
  auto nested_values = nested->as_const().member("values");
  if (!nested_values || !nested_values->as_sequence())
    return false;
  candidate.values.clear();
  if (nested->assign(*candidate_view) != e::error::invalid_argument ||
      input.nested.values.size() != 1)
    return false;
  e::serialization::context ctx{registry};
  auto json = e::serialization::json::serialize(ctx, input);
  auto snapshot = e::serialization::capture_value(ctx, input);
  if (!json || !snapshot)
    return false;
  auto decoded = e::serialization::json::deserialize<inherited_leaf>(ctx, *json);
  auto restored = e::serialization::restore_value<inherited_leaf>(ctx, *snapshot);
  if (!decoded || !restored || decoded->number != 43 || restored->number != 43 ||
      decoded->values != input.values || restored->values != input.values || decoded->middle != 5 ||
      decoded->leaf != 6 ||
      e::serialization::json::deserialize<inherited_leaf>(ctx, "{\"leaf\":6}"))
    return false;
  e::ref<inherited_base> shared = e::make_ref<inherited_leaf>(input);
  auto shared_json = e::serialization::json::serialize(ctx, shared);
  if (!shared_json)
    return false;
  auto shared_copy = e::serialization::json::deserialize<e::ref<inherited_base>>(ctx, *shared_json);
  if (!shared_copy || !*shared_copy || (*shared_copy)->number != 43 ||
      (*shared_copy)->dynamic_type().id() != e::runtime_type_of<inherited_leaf>() ||
      (*shared_copy)->values != input.values || shared_copy->get() == shared.get())
    return false;
  // ID 冲突与名称冲突均拒绝；失败不能留下已合并的半成品。
  for (bool same_id : {false, true}) {
    auto invalid = create_registry();
    if (!invalid ||
        registration<inherited_base>{*invalid}
            .property<&inherited_base::number>(property_id{1}, "number")
            .commit()
            .failed() ||
        registration<inherited_middle>{*invalid}
            .base<inherited_base>()
            .property<&inherited_middle::middle>(property_id{same_id ? 1U : 2U},
                                                 same_id ? "middle" : "number")
            .commit()
            .failed() ||
        freeze(*invalid) != e::error::invalid_argument ||
        freeze(*invalid) != e::error::invalid_argument)
      return false;
  }
  return true;
}
int main() {
  if (!inherited_properties())
    return 90;
  // Meta 独立注册表即可读写 value，不依赖 World 或资源注册。
  auto core = e::meta::create_registry();
  if (!core || e::meta::register_builtin_types(*core).failed() ||
      e::meta::register_builtin_types(*core) != e::error::invalid_argument ||
      e::meta::registration<cyclic_data>{*core}
          .base<e::meta::object>()
          .property<&cyclic_data::next>(e::meta::property_id{1}, "next")
          .commit()
          .failed() ||
      e::meta::registration<captured_data>{*core}
          .property<&captured_data::values>(e::meta::property_id{1}, "values")
          .property<&captured_data::groups>(e::meta::property_id{2}, "groups")
          .commit()
          .failed() ||
      e::meta::freeze(*core).failed())
    return 30;
  captured_data capture_input{{1, std::nullopt, 3}, {{"one", {1, 2}}, {"empty", {}}}};
  auto capture_input_view = e::meta::view(*core, std::as_const(capture_input));
  if (!capture_input_view)
    return 60;
  auto captured =
      e::serialization::capture_value(e::serialization::context{*core}, *capture_input_view);
  if (!captured)
    return 61;
  const auto original = capture_input;
  capture_input.values[0] = 100;
  capture_input.groups.clear();
  captured_data restored_data;
  auto target = e::meta::view(*core, restored_data);
  if (!target ||
      e::serialization::apply_value(e::serialization::context{*core}, *target, *captured)
          .error.failed() ||
      restored_data != original)
    return 62;
  auto typed_copy =
      e::serialization::restore_value<captured_data>(e::serialization::context{*core}, *captured);
  auto typed_capture = e::serialization::capture_value(e::serialization::context{*core}, original);
  if (!typed_copy || *typed_copy != original || !typed_capture || *typed_capture != *captured)
    return 69;
  if (e::serialization::capture_value(e::serialization::context{*core, 1}, *capture_input_view))
    return 63;
  auto bad_record = std::get<e::record_value>(captured->payload).get();
  auto bad_sequence = std::get<e::sequence_value>(bad_record.properties[0].content.payload).get();
  bad_sequence.elements[0] = e::value{std::string{"wrong"}};
  bad_record.properties[0].content = e::value{e::sequence_value{bad_sequence}};
  auto invalid_restore = e::serialization::apply_value(e::serialization::context{*core}, *target,
                                                       e::value{e::record_value{bad_record}});
  if (invalid_restore.error != e::error::invalid_argument ||
      invalid_restore.path != "/properties/1/indices/0")
    return 68;
  auto cycle = e::make_ref<cyclic_data>();
  cycle->next = cycle;
  auto cycle_view = e::meta::view(*core, std::as_const(cycle));
  auto cycle_copy = e::serialization::capture_value(e::serialization::context{*core}, *cycle_view);
  cycle->next.reset();
  if (cycle_copy || cycle_copy.error().error != e::error::invalid_argument)
    return 64;
  // 经基类引用捕获实际类型，恢复时保留拥有者并调整基类地址。
  e::ref<e::meta::object> base = cycle;
  auto base_snapshot = e::serialization::capture_value(e::serialization::context{*core}, base);
  if (!base_snapshot)
    return 70;
  auto base_copy = e::serialization::restore_value<e::ref<e::meta::object>>(
      e::serialization::context{*core}, *base_snapshot);
  if (!base_copy || !*base_copy || base_copy->get() == base.get() ||
      (*base_copy)->dynamic_type().id() != e::runtime_type_of<cyclic_data>())
    return 71;
  const std::vector<e::value_edit> edits{
      {{}, e::set_value_operation{e::value{std::int64_t{7}}}},
      {{e::meta::property_id{3}},
       e::insert_value_operation{cyclic_data::type_guid, {}, e::value{true}}},
      {{}, e::erase_value_operation{cyclic_data::type_guid}},
      {{}, e::move_value_operation{cyclic_data::type_guid, captured_data::type_guid}},
      {{}, e::put_value_operation{e::value{std::string{"x"}}, *captured}},
      {{}, e::erase_key_operation{e::value{std::string{"y"}}}}};
  auto saved_edits = e::serialization::json::serialize(e::serialization::context{*core}, edits);
  if (!saved_edits)
    return 65;
  auto restored_edits = e::serialization::json::deserialize<std::vector<e::value_edit>>(
      e::serialization::context{*core}, std::string_view{*saved_edits});
  if (!restored_edits || restored_edits->size() != edits.size())
    return 66;
  auto resaved_edits =
      e::serialization::json::serialize(e::serialization::context{*core}, *restored_edits);
  if (!resaved_edits || *saved_edits != *resaved_edits)
    return 67;
  // JSON 顶层类型与通用后端使用同一分派，不要求注册标量或 optional。
  const auto round_trip = [&]<class T>(const T& source) {
    e::serialization::context ctx{*core};
    auto encoded = e::serialization::json::serialize(ctx, source);
    if (!encoded)
      return false;
    auto decoded = e::serialization::json::deserialize<T>(ctx, std::string_view{*encoded});
    return decoded && *decoded == source;
  };
  if (!round_trip(std::int64_t{-42}) || !round_trip(std::optional<int>{}) ||
      !round_trip(std::optional<int>{17}) || !round_trip(std::monostate{}) ||
      !round_trip(e::math::matrix3{2}) || !round_trip(e::math::quaternion_xyzw(1, 2, 3, 4)))
    return 50;
  if (e::serialization::json::deserialize<std::optional<int>>(e::serialization::context{*core},
                                                              std::string_view{R"("invalid")"}))
    return 51;
  e::math::matrix3 matrix3{};
  e::math::matrix4 matrix4{};
  for (std::size_t i = 0; i < 9; ++i)
    e::math::components(matrix3)[i] = float(i + 1);
  for (std::size_t i = 0; i < 16; ++i)
    e::math::components(matrix4)[i] = float(i + 1);
  const std::array cases{e::value{},
                         e::value{true},
                         e::value{std::int64_t{-2}},
                         e::value{std::uint64_t{2}},
                         e::value{1.5F},
                         e::value{2.5},
                         e::value{std::string{"abc"}},
                         e::value{e::guid{}},
                         e::value{e::math::vector2{1, 2}},
                         e::value{e::math::vector3{1, 2, 3}},
                         e::value{e::math::vector4{1, 2, 3, 4}},
                         e::value{e::math::quaternion_xyzw(1, 2, 3, 4)},
                         e::value{matrix3},
                         e::value{matrix4},
                         e::value{e::ref<e::meta::object>{}},
                         e::value{e::record_value{}},
                         e::value{e::sequence_value{}},
                         e::value{e::map_value{}},
                         e::value{e::optional_value{}},
                         e::value{e::variant_value{}},
                         e::value{e::asset_reference_value{}},
                         e::value{e::object_reference_value{}}};
  for (const auto& item : cases) {
    auto wire = e::serialization::json::serialize(e::serialization::context{*core}, item);
    if (!wire || wire->find("\"$kind\"") == std::string::npos ||
        wire->find("\"$content\"") == std::string::npos)
      return 40;
    auto restored = e::serialization::json::deserialize<e::value>(e::serialization::context{*core},
                                                                  std::string_view{*wire});
    if (!restored || *restored != item)
      return 41;
  }
  const e::value nested{e::record_value{e::record_value_data{
      probe::type_guid,
      {{1, e::value{e::sequence_value{e::sequence_value_data{
               {e::value{e::optional_value{}},
                e::value{e::optional_value{e::optional_value_data{e::value{std::int64_t{42}}}}},
                e::value{e::variant_value{e::variant_value_data{"named", e::value{true}}}},
                e::value{e::asset_reference_value{probe::type_guid, "asset://material"}},
                e::value{e::object_reference_value{probe::type_guid, probe::type_guid}},
                e::value{e::map_value{
                    e::map_value_data{{{e::value{std::string{"key"}}, e::value{2.0F}}}}}}}}}}}}}}};
  if (!round_trip(nested))
    return 52;
  auto matrix_wire =
      e::serialization::json::serialize(e::serialization::context{*core}, e::value{matrix3});
  if (!matrix_wire ||
      matrix_wire->find("[1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0]") == std::string::npos)
    return 42;
  auto bad_matrix = e::serialization::json::deserialize<e::value>(
      e::serialization::context{*core},
      std::string_view{R"({"$kind":"matrix3","$content":[1,2]})"});
  if (bad_matrix)
    return 43;
  auto old_format = e::serialization::json::deserialize<e::value>(
      e::serialization::context{*core}, std::string_view{R"({"kind":"bool","value":true})"});
  if (old_format)
    return 44;
  e::math::components(matrix4)[2] = std::numeric_limits<float>::infinity();
  if (e::serialization::json::serialize(e::serialization::context{*core}, e::value{matrix4}))
    return 45;
  e::value edited{true};
  auto value_view = e::meta::view(*core, edited);
  if (!value_view || value_view->kind() != e::meta::type_kind::variant)
    return 46;
  auto variant = value_view->as_variant();
  if (!variant || variant->alternative_count() != cases.size() || variant->select(12).failed())
    return 47;
  auto selected = variant->value();
  if (!selected || selected->assign(e::value{matrix3}).failed() || edited != e::value{matrix3})
    return 48;
  if (variant->select(0).failed() || edited.kind() != e::value_kind::invalid)
    return 49;
  if (variant->select(cases.size()) != e::error::invalid_argument)
    return 50;
  const e::value source{std::string{"独立 Meta"}};
  auto encoded = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*core}, source);
  if (!encoded)
    return 31;
  auto restored_value = gneiss::engine::serialization::json::deserialize<e::value>(
      gneiss::engine::serialization::context{*core}, *encoded);
  if (!restored_value || *restored_value != source)
    return 32;
  // 显式 JSON 文本/字节入口共享格式，独立注册表不会退回默认表。
  auto text =
      e::serialization::json::serialize(gneiss::engine::serialization::context{*core}, source);
  if (!text ||
      *text != std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size()))
    return 34;
  auto from_text = e::serialization::json::deserialize<e::value>(
      gneiss::engine::serialization::context{*core}, std::string_view{*text});
  if (!from_text || *from_text != source)
    return 35;
  auto invalid = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*core}, e::value{});
  if (!invalid || e::meta::register_builtin_types(*core) != e::error::invalid_state)
    return 33;

  gneiss::engine::meta::registry_ref registry;
  gneiss::engine::serialization::diagnostic diagnostic;
  e::material_description material;
  std::vector<std::byte> output{std::byte{42}};
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::invalid_handle ||
      output.size() != 1)
    return 1;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::default_registry(), registry)
          .failed() ||
      e::meta::register_builtin_types().failed() || e::register_builtin_resource_types().failed())
    return 2;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::not_ready)
    return 3;
  if (gneiss::engine::meta::registration<probe>{registry}
          .extend(gneiss::engine::serialization::codec<nullptr, nullptr>)
          .commit() != e::error::invalid_argument)
    return 4;
  if (gneiss::engine::meta::registration<probe>{registry}
          .extend(gneiss::engine::serialization::codec<static_cast<decltype(&bad_encode)>(nullptr),
                                                       static_cast<decltype(&bad_decode)>(nullptr)>)
          .commit() != e::error::invalid_argument)
    return 25;
  if (gneiss::engine::meta::registration<probe>{registry}
          .extend(gneiss::engine::serialization::codec<&bad_encode, &bad_decode>)
          .extend(gneiss::engine::serialization::codec<&bad_encode, &bad_decode>)
          .commit() != e::error::invalid_argument)
    return 5;
  if (gneiss::engine::meta::registration<probe>{registry}
          .extend(gneiss::engine::serialization::codec<&bad_encode, &bad_decode>)
          .commit()
          .failed())
    return 5;
  if (e::meta::registration<move_only>{}
          .extend(gneiss::engine::serialization::codec<nullptr, &decode_move_only>)
          .commit()
          .failed())
    return 24;
  if (e::register_builtin_resource_types() != e::error::invalid_argument ||
      gneiss::engine::meta::freeze(registry).failed())
    return 6;
  gneiss::engine::serialization::type_policies binding;
  if (gneiss::test_support::assign_value(
          gneiss::engine::serialization::find_policies(registry, e::runtime_type_of<probe>()),
          binding)
          .failed() ||
      !binding.content.encode || !binding.content.decode)
    return 7;
  probe object;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::serialization::json::serialize_bytes(
              gneiss::engine::serialization::context{registry}, object),
          output, diagnostic) != e::error::out_of_memory ||
      output.size() != 1 || output[0] != std::byte{42})
    return 8;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::serialization::json::deserialize<std::remove_cvref_t<decltype(object)>>(
              gneiss::engine::serialization::context{registry}, bytes("bad")),
          object, diagnostic) != e::error::invalid_argument ||
      object.number != 3)
    return 9;
  material.parameters.color = {0.25F, 0.5F, 0.75F, 1};
  material.parameters.metallic = 0.5F;
  material.parameters.alpha_mode = e::material_alpha_mode::mask;
  material.parameters.double_sided = true;
  material.texture_uris[0] = "asset://textures/纹理\".json";
  material.parameters.sampling[0].uv_set = 1;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic)
          .failed())
    return 10;
  e::material_description restored;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
              gneiss::engine::serialization::context{registry}, output),
          restored, diagnostic)
          .failed() ||
      restored.parameters.color != material.parameters.color ||
      restored.texture_uris != material.texture_uris || restored.parameters.metallic != 0.5F ||
      restored.parameters.alpha_mode != e::material_alpha_mode::mask ||
      !restored.parameters.double_sided || restored.parameters.sampling[0].uv_set != 1)
    return 11;
  const auto saved = output;
  material.parameters.color[0] = std::numeric_limits<float>::infinity();
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::invalid_argument ||
      output != saved)
    return 12;
  material.parameters.color[0] = 2;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::invalid_argument ||
      output != saved)
    return 13;
  for (unsigned version : {1U, 2U, 3U, 4U, 5U, 7U}) {
    const auto text = std::string{R"({"format":"gneiss.material","version":)"} +
                      std::to_string(version) + R"(,"color":[1,1,1,1]})";
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                gneiss::engine::serialization::context{registry}, bytes(text)),
            restored, diagnostic) != e::error::unsupported ||
        restored.parameters.color[0] != 0.25F)
      return 14;
  }
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
              gneiss::engine::serialization::context{registry}, bytes("{")),
          restored, diagnostic) != e::error::invalid_argument ||
      restored.parameters.color[0] != 0.25F)
    return 15;
  if (e::register_builtin_resource_types() != e::error::invalid_state)
    return 16;
  gneiss::engine::meta::registry_ref no_codec;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::create_registry(), no_codec)
          .failed() ||
      gneiss::engine::meta::registration<probe>{no_codec}
          .property<&probe::number>(e::meta::property_id{1}, "number")
          .commit()
          .failed() ||
      gneiss::engine::meta::freeze(no_codec).failed())
    return 17;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::serialization::json::serialize_bytes(
              gneiss::engine::serialization::context{no_codec}, object),
          output, diagnostic)
          .failed())
    return 18;
  auto default_decoded =
      e::serialization::json::deserialize<probe>(e::serialization::context{no_codec}, output);
  if (!default_decoded || default_decoded->number != object.number)
    return 18;
  auto owned_registry = e::meta::create_registry();
  if (!owned_registry ||
      e::meta::registration<move_only>{*owned_registry}
          .extend(gneiss::engine::serialization::codec<nullptr, &decode_move_only>)
          .commit()
          .failed() ||
      e::meta::freeze(*owned_registry).failed())
    return 19;
  auto owned = gneiss::engine::serialization::json::deserialize<move_only>(
      gneiss::engine::serialization::context{*owned_registry}, bytes(R"({"number":65})"));
  if (!owned || !owned->number || *owned->number != 65)
    return 20;
  auto rejected = gneiss::engine::serialization::json::deserialize<move_only>(
      gneiss::engine::serialization::context{*owned_registry}, bytes("{}"));
  if (rejected || rejected.error().error != e::error::invalid_argument ||
      rejected.error().byte_offset != 3 || rejected.error().path != "/number")
    return 21;
  // 反射创建的拥有者可共享/擦除为 const；借用视图不承担所有权。
  auto shared_object = gneiss::engine::serialization::json::deserialize_object(
      gneiss::engine::serialization::context{*owned_registry}, e::runtime_type_of<move_only>(),
      bytes(R"({"number":65})"));
  if (!shared_object || !*shared_object)
    return 24;
  std::weak_ptr<void> weak_object = shared_object->shared();
  e::ref<const void> kept = *shared_object;
  shared_object->reset();
  if (weak_object.expired() || !kept)
    return 25;
  kept.reset();
  if (!weak_object.expired())
    return 26;
  // value 使用通用 codec，也可以作为 JSON 资产解码目标。
  auto value_decoder = e::detail::make_reflected_decoder<e::value>();
  if (!value_decoder)
    return 36;
  auto decoded_value = (*value_decoder)->decode(*encoded);
  if (!decoded_value || *std::static_pointer_cast<const e::value>(*decoded_value) != source)
    return 37;
  auto decoder = e::detail::make_reflected_decoder<move_only>();
  if (!decoder)
    return 22;
  auto input = bytes(R"({"number":42})");
  auto decoded = (*decoder)->decode(std::vector<std::byte>(input.begin(), input.end()));
  if (!decoded || *std::static_pointer_cast<const move_only>(*decoded)->number != 42)
    return 23;
  return 0;
}
