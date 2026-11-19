// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/throws.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/base/error.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
gneiss::error set_object(void*, void* target, const gneiss::value& value) {
  if (std::get<float>(value.payload) < 0) {
    return gneiss::error::invalid_argument;
  }
  if (std::get<float>(value.payload) == 99) {
    throw std::runtime_error("对象校验异常");
  }
  *static_cast<float*>(target) = std::get<float>(value.payload);
  return gneiss::error::success;
}
gneiss::error throwing_getter(void* /*context*/, const void* /*object*/,
                                      gneiss::value& /*output*/) {
  throw std::runtime_error("test callback failure");
}
} // namespace

namespace {
bool native_values() {
  namespace reflection = gneiss;
  auto registry = gneiss::meta::create_registry();
  const gneiss::guid id{gneiss::guid::from_bytes({7})};
  const gneiss::guid native_id{gneiss::guid::from_bytes({7})};
  const std::array fields{
      gneiss::meta::property_definition{.id = gneiss::property_id{1},
                                        .type_guid = gneiss::type_identity<std::string>::guid(),
                                        .access = gneiss::meta::property_access::read_write,
                                        .name = "text"}};
  std::string text = "初始文本";
  const gneiss::meta::property_binding binding{
      .kind = gneiss::value_kind::string,
      .getter =
          [](void*, const void* object, gneiss::value& output) {
            const auto& value = *static_cast<const std::string*>(object);
            output = gneiss::value{};
            output.payload = value;
            return gneiss::error::success;
          },
      .setter =
          [](void*, void* object, const gneiss::value& value) {
            static_cast<std::string*>(object)->assign(std::get<std::string>(value.payload));
            return gneiss::error::success;
          },
      .user_data = nullptr};
  if (gneiss::meta::register_type(
          registry, {.guid = id, .schema_version = 1, .name = "sample", .properties = fields},
          {&binding, 1}) != gneiss::error::success) {
    return false;
  }
  gneiss::meta::property field;
  if ((field = gneiss::meta::find_type(registry, native_id)
                   .capability<gneiss::meta::cap::object>()
                   .find_property(gneiss::property_id{1})) ||
      !field.name().empty()) {
    return false;
  }
  gneiss::value snapshot{.payload = std::string{"旧输出"}};
  if (gneiss::meta::find_type(registry, native_id) || gneiss::meta::freeze(registry).failed())
    return false;
  auto root =
      gneiss::meta::instance::from_object(gneiss::meta::find_type(registry, native_id), &text);
  if (!root)
    return false;
  auto member = root->capability<gneiss::meta::cap::object>()->member("text");
  if (!member || gneiss::test_support::assign_value(member->read_value(), snapshot).failed())
    return false;
  if (!(field = gneiss::meta::find_type(registry, native_id)
                    .capability<gneiss::meta::cap::object>()
                    .find_property(gneiss::property_id{1})) ||
      field.name() != "text" || field.id() != gneiss::property_id{1} ||
      field.type().scalar_kind() != gneiss::value_kind::string ||
      !gneiss::meta::has_flags(field.capabilities(),
                               gneiss::meta::property_capabilities::writable) ||
      (field = gneiss::meta::find_type(registry, native_id)
                   .capability<gneiss::meta::cap::object>()
                   .find_property(gneiss::property_id{99})) ||
      !field.name().empty() ||
      (field = gneiss::meta::find_type({}, native_id)
                   .capability<gneiss::meta::cap::object>()
                   .find_property(gneiss::property_id{1})) ||
      !field.name().empty()) {
    return false;
  }
  gneiss::error foreign;
  std::thread reader([&] {
    gneiss::meta::property borrowed;
    foreign = ((borrowed = gneiss::meta::find_type(registry, native_id)
                               .capability<gneiss::meta::cap::object>()
                               .find_property(gneiss::property_id{1}))
                   ? gneiss::error::success
                   : gneiss::error::not_found);
  });
  reader.join();
  if (foreign.failed()) {
    return false;
  }
  text.assign(2000, 'x');
  auto copy = snapshot;
  if (std::get<std::string>(copy.payload) != "初始文本") {
    return false;
  }
  const gneiss::value invalid{.payload = 1.0F};
  if (member->assign(invalid) != gneiss::error::invalid_argument || text.size() != 2000 ||
      member->assign(copy).failed() || text != "初始文本") {
    return false;
  }
  gneiss::meta::type metadata;
  if (!(metadata = gneiss::meta::find_type(registry, native_id)) ||
      metadata.template capability<gneiss::meta::cap::object>().properties().size() != 1 ||
      metadata.template capability<gneiss::meta::cap::object>()
              .properties()[0]
              .type()
              .scalar_kind() != gneiss::value_kind::string ||
      metadata.guid() != native_id ||
      metadata.template capability<gneiss::meta::cap::object>().properties()[0].type().guid() !=
          gneiss::type_identity<std::string>::guid() ||
      gneiss::meta::find_type(registry, gneiss::guid{}) || metadata.guid() != native_id ||
      metadata.template capability<gneiss::meta::cap::object>().properties().size() != 1 ||
      gneiss::meta::find_type({}, native_id) || metadata.guid() != native_id ||
      metadata.template capability<gneiss::meta::cap::object>().properties().size() != 1) {
    return false;
  }
  auto types = gneiss::meta::registered_types(registry);
  if (!types || types->size() != 1 || (*types)[0].guid() != native_id ||
      gneiss::meta::registered_types({}).error() != gneiss::error::invalid_handle)
    return false;
  registry.reset();
  return gneiss::test_support::assign_value(member->read_value(), snapshot) ==
             gneiss::error::success &&
         std::get<std::string>(snapshot.payload) == "初始文本";
}
} // namespace

int main() try {
  if (!native_values()) {
    return 30;
  }
  namespace reflection = gneiss;
  // 原生对象不依赖公共句柄表；最后一个内部所有者释放后必须销毁。
  auto native = gneiss::meta::create_registry();
  if (!native) {
    return 20;
  }
  const std::weak_ptr native_lifetime = native;
  if (gneiss::meta::registered_types(native).error() != gneiss::error::not_ready ||
      gneiss::meta::freeze(native).failed() || !gneiss::meta::registered_types(native)->empty())
    return 21;
  native.reset();
  if (!native_lifetime.expired() ||
      gneiss::meta::freeze(native) != gneiss::error::invalid_handle) {
    return 22;
  }
  auto state = gneiss::meta::create_registry();
  const gneiss::guid type_id{gneiss::guid::from_bytes({1})};
  std::string name = "sample";
  const std::array fields{
      gneiss::meta::property_definition{
          .id = gneiss::property_id{1U},
          .type_guid = type_id,
          .access = gneiss::meta::property_access::read_write,
          .name = "value",
      },
      gneiss::meta::property_definition{.id = gneiss::property_id{2U},
                                        .type_guid = type_id,
                                        .access = gneiss::meta::property_access::read_write,
                                        .name = "candidate"},
  };
  const gneiss::meta::type_definition definition{
      .guid = type_id,
      .schema_version = 1U,
      .name = name,
      .properties = fields,
  };
  const gneiss::meta::property_binding candidate_binding{
      .kind = gneiss::value_kind::float32,
      .getter = {},
      .setter = set_object,
      .user_data = {}};
  float candidate = 1;
  gneiss::value proposed = gneiss::value{};
  proposed.payload = 2.0F;
  const std::array bindings{gneiss::meta::property_binding{.kind = gneiss::value_kind::float32,
                                                           .getter = throwing_getter},
                            candidate_binding};
  if (gneiss::meta::register_type(state, definition, bindings).failed() ||
      gneiss::meta::find_type(state, type_id))
    return 11;
  name.assign("changed");
  if (gneiss::meta::freeze(state) != gneiss::error::success) {
    return 3;
  }
  gneiss::meta::type info;
  if (!(info = gneiss::meta::find_type(state, type_id)) || info.name() != "sample" ||
      info.template capability<gneiss::meta::cap::object>().properties().size() != 2U) {
    return 4;
  }
  {
    auto root = gneiss::meta::instance::from_object(info, &candidate);
    if (!root)
      return 12;
    auto object = root->capability<gneiss::meta::cap::object>();
    auto field = object->member("candidate");
    auto readonly = root->as_const().capability<gneiss::meta::cap::object>()->member("value");
    if (!field || !readonly || object->member("value").error() != gneiss::error::unsupported ||
        gneiss::meta::instance::from_object(info, nullptr).error() !=
            gneiss::error::invalid_argument ||
        field->assign(proposed).failed() || candidate != 2 ||
        gneiss::meta::register_type(state, definition, bindings) != gneiss::error::invalid_state)
      return 12;
    proposed.payload = -1.0F;
    if (field->assign(proposed) != gneiss::error::invalid_argument || candidate != 2)
      return 13;
    proposed.payload = 99.0F;
    if (!gneiss::test_support::throws<std::runtime_error>([&] { (void)field->assign(proposed); }) ||
        candidate != 2)
      return 14;
    proposed.payload = true;
    if (field->assign(proposed) != gneiss::error::invalid_argument || candidate != 2)
      return 15;
    if (!gneiss::test_support::throws<std::runtime_error>([&] { (void)readonly->read_value(); }))
      return 5;
  }
  gneiss::error query{};
  std::uint32_t count{};
  std::thread worker([&] {
    auto types = gneiss::meta::registered_types(state);
    query = types ? gneiss::error::success : types.error();
    if (types)
      count = static_cast<std::uint32_t>(types->size());
  });
  worker.join();
  if (query != gneiss::error::success || count != 1U) {
    return 6;
  }
  // 原生引用直接拥有 Registry，无公共句柄表；最后一个引用释放后销毁。
  const std::weak_ptr lifetime = state;
  auto retained = state;
  auto published = gneiss::meta::registered_types(state);
  if (!published)
    return 17;
  state.reset();
  if (lifetime.expired() || !gneiss::meta::registered_types(retained))
    return 7;
  retained.reset();
  if (lifetime.expired() || info.name() != "sample")
    return 16;
  info = {};
  if (lifetime.expired() || published->size() != 1 || (*published)[0].name() != "sample")
    return 18;
  // 迭代器也持有注册表，范围释放后仍能读取其冻结记录。
  auto cursor = published->begin();
  published = gneiss::error::invalid_state;
  if (lifetime.expired() || (*cursor).guid() != type_id)
    return 19;
  cursor = {};
  return lifetime.expired() ? 0 : 8;
} catch (...) {
  return 10;
}
