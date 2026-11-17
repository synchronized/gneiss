// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/throws.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/meta/registration.hpp>

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
  gneiss::meta::registry_ref registry;
  const gneiss::guid id{gneiss::guid::from_bytes({7})};
  const gneiss::guid native_id{gneiss::guid::from_bytes({7})};
  const std::array fields{gneiss::meta::property_definition{
      .id = gneiss::meta::property_id{1},
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
  if (gneiss::test_support::assign_value(gneiss::meta::create_registry(), registry) !=
          gneiss::error::success ||
      gneiss::meta::register_type(
          registry, {.guid = id, .schema_version = 1, .name = "sample", .properties = fields}) !=
          gneiss::error::success ||
      gneiss::meta::bind_property(registry, id, gneiss::meta::property_id{1},
                                          binding) != gneiss::error::success) {
    return false;
  }
  gneiss::meta::property field;
  if ((field = gneiss::meta::find_property(registry, native_id,
                                                   gneiss::meta::property_id{1})) ||
      !field.name().empty()) {
    return false;
  }
  gneiss::value snapshot{.payload = std::string{"旧输出"}};
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, native_id,
                                              gneiss::meta::property_id{1}, &text),
          snapshot) != gneiss::error::not_ready ||
      std::get<std::string>(snapshot.payload) != "旧输出" ||
      gneiss::meta::freeze(registry) != gneiss::error::success) {
    return false;
  }
  if (gneiss::test_support::assign_value(
          gneiss::meta::read_property(registry, native_id,
                                              gneiss::meta::property_id{1}, &text),
          snapshot)
          .failed()) {
    return false;
  }
  if (!(field = gneiss::meta::find_property(registry, native_id,
                                                    gneiss::meta::property_id{1})) ||
      field.name() != "text" || field.id() != gneiss::meta::property_id{1} ||
      field.type().scalar_kind() != gneiss::value_kind::string ||
      !gneiss::meta::has_flags(field.capabilities(),
                                       gneiss::meta::property_capabilities::writable) ||
      (field = gneiss::meta::find_property(registry, native_id,
                                                   gneiss::meta::property_id{99})) ||
      !field.name().empty() ||
      (field = gneiss::meta::find_property({}, native_id,
                                                   gneiss::meta::property_id{1})) ||
      !field.name().empty()) {
    return false;
  }
  gneiss::error foreign;
  std::thread reader([&] {
    gneiss::meta::property borrowed;
    foreign = ((borrowed = gneiss::meta::find_property(
                    registry, native_id, gneiss::meta::property_id{1}))
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
  if (gneiss::meta::write_property(registry, native_id,
                                           gneiss::meta::property_id{1}, &text,
                                           invalid) != gneiss::error::invalid_argument ||
      text.size() != 2000 ||
      gneiss::meta::write_property(registry, native_id,
                                           gneiss::meta::property_id{1}, &text, copy)
          .failed() ||
      text != "初始文本") {
    return false;
  }
  gneiss::meta::type metadata;
  if (!(metadata = gneiss::meta::find_type(registry, native_id)) ||
      metadata.properties().size() != 1 ||
      metadata.properties()[0].type().scalar_kind() != gneiss::value_kind::string ||
      metadata.guid() != native_id ||
      metadata.properties()[0].type().guid() !=
          gneiss::type_identity<std::string>::guid() ||
      gneiss::meta::find_type(registry, gneiss::guid{}) ||
      metadata.guid() != native_id || metadata.properties().size() != 1 ||
      gneiss::meta::find_type({}, native_id) || metadata.guid() != native_id ||
      metadata.properties().size() != 1) {
    return false;
  }
  if (gneiss::test_support::assign_value(gneiss::meta::type_at(registry, 0), metadata)
          .failed() ||
      metadata.guid() != native_id || metadata.properties().size() != 1 ||
      gneiss::test_support::assign_value(gneiss::meta::type_at(registry, 1), metadata) !=
          gneiss::error::not_found ||
      metadata.guid() != native_id ||
      gneiss::test_support::assign_value(gneiss::meta::type_at({}, 0), metadata) !=
          gneiss::error::invalid_handle ||
      metadata.guid() != native_id)
    return false;
  registry.reset();
  return gneiss::test_support::assign_value(
             gneiss::meta::read_property(registry, native_id,
                                                 gneiss::meta::property_id{1}, &text),
             snapshot) == gneiss::error::invalid_handle &&
         std::get<std::string>(snapshot.payload) == "初始文本";
}
} // namespace

int main() try {
  if (!native_values()) {
    return 30;
  }
  namespace reflection = gneiss;
  // 原生对象不依赖公共句柄表；最后一个内部所有者释放后必须销毁。
  gneiss::meta::registry_ref native;
  if (gneiss::test_support::assign_value(gneiss::meta::create_registry(), native) !=
          gneiss::error::success ||
      !native) {
    return 20;
  }
  const std::weak_ptr native_lifetime = native;
  std::uint32_t native_count = 99;
  if (gneiss::test_support::assign_value(gneiss::meta::type_count(native), native_count) !=
          gneiss::error::not_ready ||
      native_count != 99 ||
      gneiss::meta::freeze(native) != gneiss::error::success ||
      gneiss::test_support::assign_value(gneiss::meta::type_count(native), native_count) !=
          gneiss::error::success ||
      native_count != 0) {
    return 21;
  }
  native.reset();
  if (!native_lifetime.expired() ||
      gneiss::meta::freeze(native) != gneiss::error::invalid_handle) {
    return 22;
  }
  gneiss::meta::registry_ref state;
  if (gneiss::test_support::assign_value(gneiss::meta::create_registry(), state).failed())
    return 1;
  const gneiss::guid type_id{gneiss::guid::from_bytes({1})};
  std::string name = "sample";
  const std::array fields{
      gneiss::meta::property_definition{
          .id = gneiss::meta::property_id{1U},
          .type_guid = type_id,
          .access = gneiss::meta::property_access::read_write,
          .name = "value",
      },
      gneiss::meta::property_definition{
          .id = gneiss::meta::property_id{2U},
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
  if (gneiss::meta::register_type(state, definition) != gneiss::error::success ||
      gneiss::meta::bind_property(state, type_id, gneiss::meta::property_id{1U},
                                          {.kind = gneiss::value_kind::float32,
                                           .getter = throwing_getter,
                                           .setter = nullptr}) != gneiss::error::success) {
    return 2;
  }
  const gneiss::meta::property_binding candidate_binding{
      .kind = gneiss::value_kind::float32,
      .getter = {},
      .setter = set_object,
      .user_data = {}};
  float candidate = 1;
  gneiss::value proposed = gneiss::value{};
  proposed.payload = 2.0F;
  if (gneiss::meta::bind_property(state, type_id, gneiss::meta::property_id{2U},
                                          candidate_binding) != gneiss::error::success ||
      gneiss::meta::bind_property(state, type_id, gneiss::meta::property_id{2U},
                                          candidate_binding) != gneiss::error::success ||
      gneiss::meta::write_property(state, type_id, gneiss::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::error::not_ready) {
    return 11;
  }
  name.assign("changed");
  if (gneiss::meta::freeze(state) != gneiss::error::success) {
    return 3;
  }
  gneiss::meta::type info;
  if (!(info = gneiss::meta::find_type(state, type_id)) || info.name() != "sample" ||
      info.properties().size() != 2U) {
    return 4;
  }
  if (gneiss::meta::write_property(state, type_id, gneiss::meta::property_id{1U},
                                           &candidate,
                                           proposed) != gneiss::error::unsupported ||
      gneiss::meta::write_property(state, type_id, gneiss::meta::property_id{2U},
                                           nullptr,
                                           proposed) != gneiss::error::invalid_argument ||
      gneiss::meta::write_property(state, type_id, gneiss::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::error::success ||
      candidate != 2 ||
      gneiss::meta::bind_property(state, type_id, gneiss::meta::property_id{2U},
                                          candidate_binding) !=
          gneiss::error::invalid_state) {
    return 12;
  }
  proposed.payload = -1.0F;
  if (gneiss::meta::write_property(state, type_id, gneiss::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::error::invalid_argument ||
      candidate != 2) {
    return 13;
  }
  proposed.payload = 99.0F;
  if (!gneiss::test_support::throws<std::runtime_error>([&] {
        (void)(gneiss::meta::write_property(
            state, type_id, gneiss::meta::property_id{2U}, &candidate, proposed));
      }) ||
      candidate != 2) {
    return 14;
  }
  proposed.payload = true;
  if (gneiss::meta::write_property(state, type_id, gneiss::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::error::invalid_argument ||
      candidate != 2) {
    return 15;
  }
  gneiss::value value;
  if (!gneiss::test_support::throws<std::runtime_error>([&] {
        (void)(gneiss::test_support::assign_value(
            gneiss::meta::read_property(state, type_id,
                                                gneiss::meta::property_id{1U}, &candidate),
            value));
      }) ||
      value.kind() != gneiss::value_kind::invalid) {
    return 5;
  }
  gneiss::error query{};
  std::uint32_t count{};
  std::thread worker([&] {
    query = gneiss::test_support::assign_value(gneiss::meta::type_count(state), count);
  });
  worker.join();
  if (query != gneiss::error::success || count != 1U) {
    return 6;
  }
  // 原生引用直接拥有 Registry，无公共句柄表；最后一个引用释放后销毁。
  const std::weak_ptr lifetime = state;
  auto retained = state;
  state.reset();
  if (lifetime.expired() ||
      gneiss::test_support::assign_value(gneiss::meta::type_count(retained), count)
          .failed())
    return 7;
  retained.reset();
  if (lifetime.expired() || info.name() != "sample")
    return 16;
  info = {};
  return lifetime.expired() ? 0 : 8;
} catch (...) {
  return 10;
}
