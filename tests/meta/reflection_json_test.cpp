// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/serialization/serialization.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/resource/asset_decoder.hpp"
#include <future>
#include <limits>
#include <stdexcept>
namespace e = gneiss::engine;
namespace m = e::meta;
struct sample {
  inline static constexpr std::string_view type_name = "json_sample";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({61, 2});
  bool enabled = true;
  std::int64_t signed_number = -4;
  std::uint64_t unsigned_number = 5;
  float speed = 1;
  double precise = 2;
  std::string name = "默认";
  e::guid identity{};
  e::math::vector2 uv{0.25F, 0.75F};
  e::math::vector3 position{1, 2, 3};
  e::math::quaternion rotation{1, 0, 0, 0};
  float computed() const { return speed * 2; }
};
gneiss::engine::serialization::diagnostic validate_sample(const sample& object) {
  if (object.speed <= 0)
    return {.path = "/speed", .message = "速度必须大于零", .error = e::error::invalid_argument};
  return {};
}
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
e::error register_sample(const m::registry_ref& registry, std::string_view name = "name") {
  return m::registration::type<sample>(registry)
      .property<&sample::enabled>(m::property_id{1}, "enabled")(
          gneiss::engine::serialization::field_policy{.required = true})
      .property<&sample::signed_number>(m::property_id{2}, "signed")
      .property<&sample::unsigned_number>(m::property_id{3}, "unsigned")
      .property<&sample::speed>(m::property_id{4}, "speed")
      .property<&sample::precise>(m::property_id{5}, "precise")
      .property<&sample::name>(m::property_id{6}, name)
      .property<&sample::identity>(m::property_id{7}, "identity")
      .property<&sample::position>(m::property_id{8}, "position")
      .property<&sample::rotation>(m::property_id{9}, "rotation")
      .property<&sample::uv>(m::property_id{11}, "uv")
      .readonly<&sample::computed>(m::property_id{10}, "computed")
      .extend(gneiss::engine::serialization::validation<&validate_sample>)
      .commit();
}
struct tracked {
  inline static constexpr std::string_view type_name = "tracked_json";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({61, 3});
  inline static int alive = 0;
  tracked() { ++alive; }
  ~tracked() { --alive; }
  float value = 0;
};
e::error reject(const tracked&) { return e::error::invalid_argument; }
e::result<void*, gneiss::engine::serialization::diagnostic>
custom_encode(const e::serialization::write_context&, e::serialization::writer& output,
              const sample&) {
  auto root = output.object();
  if (!root || !output.member(root, "custom", output.string("yes")))
    return gneiss::engine::serialization::diagnostic{.error = e::error::out_of_memory};
  return root;
}
e::result<sample, gneiss::engine::serialization::diagnostic>
custom_decode(const e::serialization::read_context&, e::serialization::reader&, const void*) {
  return sample{};
}
enum class named_number : std::int32_t { negative = -7, positive = 9 };
struct enum_sample {
  static constexpr std::string_view type_name = "json_enum_sample";
  static constexpr e::guid type_guid = e::guid::from_bytes({61, 9});
  named_number named{named_number::negative};
  named_number numeric{named_number::negative};
};
bool enum_json_contract() {
  auto registry = m::create_registry();
  if (!registry ||
      m::registration::enumeration<named_number>(*registry)
          .value("negative", named_number::negative)
          .value("positive", named_number::positive)
          .commit()
          .failed() ||
      m::registration::type<enum_sample>(*registry)
          .property<&enum_sample::named>(m::property_id{1}, "named")(
              gneiss::engine::serialization::field_policy{.required = true, .enum_name = true})
          .property<&enum_sample::numeric>(m::property_id{2}, "numeric")
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit()
          .failed() ||
      m::freeze(*registry).failed())
    return false;
  auto encoded = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{*registry}, enum_sample{});
  if (!encoded)
    return false;
  const std::string_view text(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  if (text.find(R"("named":"negative")") == text.npos || text.find(R"("numeric":-7)") == text.npos)
    return false;
  auto decoded = gneiss::engine::serialization::json::decode<enum_sample>(
      gneiss::engine::serialization::read_context{*registry}, *encoded);
  if (!decoded || decoded->named != named_number::negative ||
      decoded->numeric != named_number::negative)
    return false;
  for (const auto invalid : {R"({"named":-7})", R"({"named":"unknown"})", R"({"named":null})"}) {
    auto rejected = gneiss::engine::serialization::json::decode<enum_sample>(
        gneiss::engine::serialization::read_context{*registry}, bytes(invalid));
    if (rejected || rejected.error().path != "/named")
      return false;
  }
  enum_sample unknown;
  unknown.named = static_cast<named_number>(10);
  return !gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{*registry}, unknown);
}
struct reference_fields {
  static constexpr std::string_view type_name = "reference_fields";
  static constexpr e::guid type_guid = e::guid::literal("42e05d27-c830-4468-bb31-3320d7ea8aaf");
  e::ref<sample> default_reference;
  e::ref<sample> overridden_reference;
  int transient = 7;
};
bool field_reference_contract() {
  namespace ser = e::serialization;
  for (bool type_polymorphic : {false, true}) {
    auto created = m::create_registry();
    if (!created)
      return false;
    const auto registry = *created;
    if (m::registration::type<sample>(registry)
            .property<&sample::enabled>(m::property_id{1}, "enabled")
            .metadata(ser::reference_policy{.polymorphic = type_polymorphic})
            .commit()
            .failed() ||
        m::registration::type<reference_fields>(registry)
            .property<&reference_fields::default_reference>(m::property_id{1}, "default")
            .property<&reference_fields::overridden_reference>(m::property_id{2},
                                                               "override")(ser::field_policy{
                .reference = ser::reference_policy{.polymorphic = !type_polymorphic}})
            .property<&reference_fields::transient>(m::property_id{3},
                                                    "transient")(ser::field_policy{.save = false})
            .commit()
            .failed() ||
        m::freeze(registry).failed())
      return false;
    reference_fields input{e::make_ref<sample>(), e::make_ref<sample>(), 42};
    input.overridden_reference->enabled = false;
    const ser::write_context ctx{registry};
    ser::read_context read_ctx{registry};
    auto encoded = ser::json::encode_bytes(ctx, input);
    if (!encoded)
      return false;
    std::string text(reinterpret_cast<const char*>(encoded->data()), encoded->size());
    const std::string_view plain =
        type_polymorphic ? R"("override":{"enabled":false})" : R"("default":{"enabled":true})";
    if (text.find(plain) == text.npos || text.find("$type_guid") == text.npos ||
        text.find("transient") != text.npos)
      return false;
    auto decoded = ser::json::decode<reference_fields>(read_ctx, *encoded);
    auto snapshot = ser::capture_value(ctx, input);
    if (!decoded || !snapshot || !decoded->default_reference || !decoded->overridden_reference ||
        decoded->overridden_reference->enabled || decoded->transient != 7)
      return false;
    auto restored = ser::restore_value<reference_fields>(read_ctx, *snapshot);
    if (!restored || !restored->overridden_reference || restored->overridden_reference->enabled ||
        restored->transient != 7 || restored->overridden_reference == input.overridden_reference)
      return false;
    // 捕获的数据与随后修改的引用对象隔离。
    input.overridden_reference->enabled = true;
    if (restored->overridden_reference->enabled)
      return false;
    input.default_reference.reset();
    input.overridden_reference.reset();
    auto nulls = ser::json::encode_bytes(ctx, input);
    auto null_snapshot = ser::capture_value(ctx, input);
    if (!nulls || !null_snapshot)
      return false;
    auto empty = ser::json::decode<reference_fields>(read_ctx, *nulls);
    auto restored_empty = ser::restore_value<reference_fields>(read_ctx, *null_snapshot);
    if (!empty || !restored_empty || empty->default_reference || empty->overridden_reference ||
        restored_empty->default_reference || restored_empty->overridden_reference)
      return false;
  }
  return true;
}
bool asymmetric_reference_snapshot_contract() {
  namespace ser = e::serialization;
  for (bool decode_only : {false, true}) {
    auto registry = m::create_registry();
    if (!registry)
      return false;
    ser::reference_policy policy;
    if (decode_only)
      policy.decode = [](const ser::read_context&, ser::reader&,
                         const void*) -> e::result<e::ref<void>, ser::diagnostic> {
        return e::ref<void>{e::make_ref<sample>()};
      };
    else
      policy.encode = [](const ser::write_context&, ser::writer& out,
                         const void*) -> e::result<void*, ser::diagnostic> {
        return out.string("asset://sample");
      };
    if (m::registration::type<sample>(*registry)
            .property<&sample::enabled>(m::property_id{1}, "enabled")
            .metadata(policy)
            .commit()
            .failed() ||
        m::freeze(*registry).failed())
      return false;
    ser::write_context ctx{*registry};
    ser::read_context read_ctx{*registry};
    auto captured = ser::capture_value(ctx, e::make_ref<sample>());
    if (captured || captured.error().error != e::error::unsupported)
      return false;
    e::ref<sample> empty;
    if (!ser::capture_value(ctx, empty))
      return false;
  }
  return true;
}
bool external_field_contract() {
  namespace ser = e::serialization;
  const ser::reference_policy external{
      .encode = [](const ser::write_context&, ser::writer& out, const void*)
          -> e::result<void*, ser::diagnostic> { return out.string("asset://sample"); },
      .decode = [](const ser::read_context&, ser::reader& in,
                   const void* node) -> e::result<e::ref<void>, ser::diagnostic> {
        if (!in.is_string(node) || in.string(node) != "asset://sample")
          return ser::diagnostic{.error = e::error::invalid_argument};
        return e::ref<void>{e::make_ref<sample>()};
      },
      .capture = [](e::guid type, const void*) -> e::result<e::asset_reference_value> {
        return e::asset_reference_value{type, "asset://sample"};
      }};
  for (bool external_default : {false, true}) {
    auto registry = m::create_registry();
    if (!registry ||
        m::registration::type<sample>(*registry)
            .property<&sample::enabled>(m::property_id{1}, "enabled")
            .metadata(external_default ? external : ser::reference_policy{})
            .commit()
            .failed() ||
        m::registration::type<reference_fields>(*registry)
            .property<&reference_fields::default_reference>(m::property_id{1}, "default")
            .property<&reference_fields::overridden_reference>(m::property_id{2},
                                                               "override")(ser::field_policy{
                .reference = external_default ? ser::reference_policy{} : external})
            .commit()
            .failed() ||
        m::freeze(*registry).failed())
      return false;
    const ser::write_context ctx{*registry};
    ser::read_context read_ctx{*registry};
    reference_fields input{e::make_ref<sample>(), e::make_ref<sample>()};
    auto json = ser::json::encode_bytes(ctx, input);
    auto captured = ser::capture_value(ctx, input);
    if (!json || !captured)
      return false;
    const std::string text(reinterpret_cast<const char*>(json->data()), json->size());
    const auto expected =
        external_default ? R"("default":"asset://sample")" : R"("override":"asset://sample")";
    if (text.find(expected) == text.npos || !ser::json::decode<reference_fields>(read_ctx, *json))
      return false;
    const auto* record = std::get_if<e::record_value>(&captured->payload);
    if (!record || record->get().properties.size() != 2)
      return false;
    const auto external_index = external_default ? 0U : 1U;
    if (!std::holds_alternative<e::asset_reference_value>(
            record->get().properties[external_index].content.payload) ||
        !std::holds_alternative<e::record_value>(
            record->get().properties[1U - external_index].content.payload))
      return false;
    auto restored = ser::restore_value<reference_fields>(read_ctx, *captured);
    if (restored || restored.error().error != e::error::invalid_state)
      return false; // 外部身份不能静默还原为空对象，必须提供解析器。
  }
  return true;
}
bool nested_json_contract();
int main() try {
  if (!asymmetric_reference_snapshot_contract())
    return 29;
  if (!external_field_contract())
    return 28;
  if (!field_reference_contract())
    return 27;
  if (!enum_json_contract())
    return 26;
  if (!nested_json_contract())
    return 25;
  auto created = m::create_registry();
  if (!created)
    return 1;
  auto registry = *created;
  if (register_sample(registry).failed())
    return 2;
  if (gneiss::engine::serialization::json::encode_bytes(
          gneiss::engine::serialization::write_context{registry}, sample{})
          .error()
          .error != e::error::not_ready)
    return 3;
  if (m::registration::type<tracked>(registry)
          .property<&tracked::value>(m::property_id{1}, "value")
          .extend(gneiss::engine::serialization::validation<&reject>)
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return 4;
  sample original;
  original.signed_number = std::numeric_limits<std::int64_t>::min();
  original.unsigned_number = std::numeric_limits<std::uint64_t>::max();
  original.name = std::string("中文\"\\");
  original.identity = sample::type_guid;
  original.rotation = {0.9F, 0.1F, 0.2F, 0.3F};
  auto encoded = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{registry}, original);
  if (!encoded)
    return 5;
  const std::string_view text{reinterpret_cast<const char*>(encoded->data()), encoded->size()};
  if (text.find("computed") != std::string_view::npos)
    return 6;
  auto decoded = gneiss::engine::serialization::json::decode<sample>(
      gneiss::engine::serialization::read_context{registry}, *encoded);
  if (!decoded || decoded->signed_number != original.signed_number ||
      decoded->unsigned_number != original.unsigned_number || decoded->name != original.name ||
      decoded->identity != original.identity || decoded->position != original.position ||
      decoded->rotation != original.rotation || decoded->uv != original.uv)
    return 7;
  original.identity = {};
  auto nil_encoded = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{registry}, original);
  if (!nil_encoded)
    return 40;
  const std::string nil_text(reinterpret_cast<const char*>(nil_encoded->data()),
                             nil_encoded->size());
  if (nil_text.find("\"identity\":null") == std::string::npos)
    return 42;
  auto zero_string = gneiss::engine::serialization::json::decode<sample>(
      gneiss::engine::serialization::read_context{registry},
      bytes(R"({"enabled":true,"identity":"00000000-0000-0000-0000-000000000000"})"));
  if (!zero_string || zero_string->identity)
    return 43;
  auto nil_decoded = gneiss::engine::serialization::json::decode<sample>(
      gneiss::engine::serialization::read_context{registry}, *nil_encoded);
  if (!nil_decoded || nil_decoded->identity)
    return 41;
  auto defaults = gneiss::engine::serialization::json::decode<sample>(
      gneiss::engine::serialization::read_context{registry},
      bytes(R"({"enabled":false,"unknown":1,"computed":99})"));
  if (!defaults || defaults->enabled || defaults->name != "默认" || defaults->speed != 1)
    return 8;
  const auto fails = [&](std::string_view json, std::string_view path) {
    auto result = gneiss::engine::serialization::json::decode<sample>(
        gneiss::engine::serialization::read_context{registry}, bytes(json));
    return !result && result.error().error == e::error::invalid_argument &&
           result.error().path == path;
  };
  if (!fails("{}", "/enabled") || !fails(R"({"enabled":null})", "/enabled") ||
      !fails(R"({"enabled":true,"enabled":false})", "/enabled") ||
      !fails(R"({"enabled":true,"speed":0})", "/speed") ||
      !fails(R"({"enabled":true,"speed":1e100})", "/speed") ||
      !fails(R"({"enabled":true,"unsigned":-1})", "/unsigned") ||
      !fails(R"({"enabled":true,"signed":18446744073709551615})", "/signed") ||
      !fails(R"({"enabled":true,"signed":2.5})", "/signed") ||
      !fails(R"({"enabled":true,"position":[1,2]})", "/position") ||
      !fails(R"({"enabled":true,"identity":"bad"})", "/identity") ||
      !fails(R"({"enabled":true,"identity":42})", "/identity"))
    return 9;
  if (!fails(R"({"enabled":true,"uv":[1]})", "/uv") ||
      !fails(R"({"enabled":true,"uv":[1,2,3]})", "/uv") ||
      !fails(R"({"enabled":true,"uv":[1,1e100]})", "/uv/1") ||
      !fails(R"({"enabled":true,"uv":[1,"bad"]})", "/uv/1"))
    return 31;
  auto view = m::view(registry, original);
  if (!view)
    return 32;
  auto uv = view->member(m::property_id{11});
  if (!uv || uv->read_value()->kind() != e::value_kind::vec2 ||
      uv->assign(e::value{e::math::vector2{2, 3}}).failed() || original.uv.x != 2 ||
      uv->assign(e::value{e::math::vector3{2, 3, 4}}).ok())
    return 33;
  original.uv.y = std::numeric_limits<float>::infinity();
  auto invalid_uv = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{registry}, original);
  if (invalid_uv || invalid_uv.error().path != "/uv")
    return 34;
  original.uv = {0.25F, 0.75F};
  original.speed = std::numeric_limits<float>::infinity();
  auto invalid = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{registry}, original);
  // getter 产生非法属性值沿用 Meta 的 internal 语义，并保留字段位置。
  if (invalid || invalid.error().error != e::error::internal || invalid.error().path != "/speed")
    return 10;
  original.speed = -1;
  invalid = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{registry}, original);
  if (invalid || invalid.error().path != "/speed")
    return 11;
  auto failed = gneiss::engine::serialization::json::decode_object(
      gneiss::engine::serialization::read_context{registry}, e::runtime_type_of<tracked>(),
      bytes(R"({"value":1})"));
  if (failed || tracked::alive != 0)
    return 12;
  auto failed_field = gneiss::engine::serialization::json::decode_object(
      gneiss::engine::serialization::read_context{registry}, e::runtime_type_of<tracked>(),
      bytes(R"({"value":"bad"})"));
  if (failed_field || tracked::alive != 0)
    return 13;
  // 同一个 C++ 类型在隔离注册表使用不同属性名，不能回退到默认注册表。
  auto other = m::create_registry();
  if (!other || register_sample(*other, "a/b~c").failed() || m::freeze(*other).failed())
    return 14;
  auto escaped = gneiss::engine::serialization::json::decode<sample>(
      gneiss::engine::serialization::read_context{*other}, bytes(R"({"enabled":true,"a/b~c":2})"));
  if (escaped || escaped.error().path != "/a~1b~0c")
    return 15;
  // 无 registry 的视图编解码必须保留隔离表的字段名，不能查询默认表。
  sample isolated_value;
  auto isolated_view = m::view(*other, isolated_value);
  if (!isolated_view ||
      gneiss::engine::serialization::json::read_into(
          *isolated_view, bytes(R"({"enabled":true,"a/b~c":"isolated"})"))
          .failed() ||
      isolated_value.name != "isolated")
    return 90;
  auto isolated_bytes =
      gneiss::engine::serialization::json::encode_instance(isolated_view->as_const());
  if (!isolated_bytes || std::string_view(reinterpret_cast<const char*>(isolated_bytes->data()),
                                          isolated_bytes->size())
                                 .find("a/b~c") == std::string_view::npos)
    return 91;
  isolated_view = e::error::invalid_state;
  // CPU 资源入口统一使用默认表；独立表不再由解码适配器持有。
  auto shared = m::default_registry();
  if (!shared || register_sample(*shared).failed() || m::freeze().failed())
    return 16;
  auto loader = e::detail::make_reflected_decoder<sample>();
  auto weak = std::weak_ptr<e::meta::type_registry_state>(*other);
  other->reset();
  if (!weak.expired() || !loader || !(*loader)->decode(*encoded))
    return 17;
  auto task = std::async(std::launch::async, [&] {
    return static_cast<bool>(gneiss::engine::serialization::json::decode<sample>(
        gneiss::engine::serialization::read_context{registry}, *encoded));
  });
  if (!gneiss::engine::serialization::json::decode<sample>(
          gneiss::engine::serialization::read_context{registry}, *encoded) ||
      !task.get())
    return 18;
  auto custom = m::create_registry();
  if (!custom ||
      m::registration::type<sample>(*custom)
          .extend(gneiss::engine::serialization::codec<&custom_encode, &custom_decode>)
          .commit()
          .failed() ||
      m::freeze(*custom).failed())
    return 19;
  auto custom_output = gneiss::engine::serialization::json::encode_bytes(
      gneiss::engine::serialization::write_context{*custom}, sample{});
  if (!custom_output ||
      std::string_view(reinterpret_cast<const char*>(custom_output->data()),
                       custom_output->size()) != R"({"custom":"yes"})" ||
      !gneiss::engine::serialization::json::decode<sample>(
          gneiss::engine::serialization::read_context{*custom}, bytes("{}")))
    return 20;
  auto fallback = m::create_registry();
  if (!fallback ||
      m::registration::type<sample>(*fallback)
          .property<&sample::enabled>(m::property_id{1}, "enabled")
          .extend(gneiss::engine::serialization::codec<&custom_encode, nullptr>)
          .commit()
          .failed() ||
      m::freeze(*fallback).failed())
    return 22;
  auto fallback_decoded = gneiss::engine::serialization::json::decode<sample>(
      gneiss::engine::serialization::read_context{*fallback}, bytes(R"({"enabled":false})"));
  if (!fallback_decoded || fallback_decoded->enabled)
    return 23;
  // Meta 接受不透明扩展；序列化边界负责拒绝冲突的字段策略。
  for (int invalid_case : {0, 1, 2, 3}) {
    const bool invalid_enum = invalid_case == 1;
    e::serialization::field_policy policy{.required = invalid_case == 0, .enum_name = invalid_enum};
    if (invalid_case == 2) {
      policy.save = false;
      policy.required = true;
    }
    if (invalid_case == 3)
      policy.reference = e::serialization::reference_policy{};
    auto registry = m::create_registry();
    if (!registry ||
        m::registration::type<sample>(*registry)
            .property<&sample::enabled>(m::property_id{1}, "enabled",
                                        invalid_case != 0 ? m::property_access::read_write
                                                          : m::property_access::read_only)(policy)
            .commit()
            .failed() ||
        m::freeze(*registry).failed())
      return 24;
    const e::serialization::write_context ctx{*registry};
    e::serialization::read_context read_ctx{*registry};
    auto encoded = e::serialization::json::encode_bytes(ctx, sample{});
    auto decoded = e::serialization::json::decode<sample>(read_ctx, bytes(R"({"enabled":true})"));
    if (encoded || decoded || encoded.error().error != e::error::invalid_argument ||
        decoded.error().error != e::error::invalid_argument || encoded.error().path != "/enabled" ||
        decoded.error().path != "/enabled")
      return 25;
  }
  return 0;
} catch (...) {
  return 99;
}
