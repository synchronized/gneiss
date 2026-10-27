// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/meta/registration.hpp"
#include "engine/core/result.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
gneiss::engine::result set_object(void*, void* target, const gneiss::engine::value& value) {
  if (std::get<float>(value.payload) < 0) {
    return gneiss::engine::result::invalid_argument;
  }
  if (std::get<float>(value.payload) == 99) {
    throw std::runtime_error("对象校验异常");
  }
  *static_cast<float*>(target) = std::get<float>(value.payload);
  return gneiss::engine::result::success;
}
gneiss::engine::result throwing_getter(void* /*context*/, const void* /*object*/,
                                       gneiss::engine::value& /*output*/) {
  throw std::runtime_error("test callback failure");
}
} // namespace

namespace {
bool native_values() {
  namespace reflection = gneiss::engine;
  gneiss::engine::meta::registry_ref registry;
  const gneiss::engine::guid id{gneiss::engine::guid::from_bytes({7})};
  const gneiss::engine::guid native_id{gneiss::engine::guid::from_bytes({7})};
  const std::array fields{
      gneiss::engine::meta::property_definition{.id = gneiss::engine::meta::property_id{1},
                                                .type_guid = id,
                                                .flags = gneiss::engine::meta::property_flags::none,
                                                .name = "text"}};
  std::string text = "初始文本";
  const gneiss::engine::meta::property_binding binding{
      .kind = gneiss::engine::value_kind::string,
      .getter =
          [](void*, const void* object, gneiss::engine::value& output) {
            const auto& value = *static_cast<const std::string*>(object);
            output = gneiss::engine::value{};
            output.payload = value;
            return gneiss::engine::result::success;
          },
      .setter =
          [](void*, void* object, const gneiss::engine::value& value) {
            static_cast<std::string*>(object)->assign(std::get<std::string>(value.payload));
            return gneiss::engine::result::success;
          },
      .user_data = nullptr};
  if (gneiss::engine::meta::create_registry(registry) != gneiss::engine::result::success ||
      gneiss::engine::meta::register_type(
          registry, {.guid = id, .schema_version = 1, .name = "sample", .properties = fields}) !=
          gneiss::engine::result::success ||
      gneiss::engine::meta::bind_property(registry, id, gneiss::engine::meta::property_id{1},
                                          binding) != gneiss::engine::result::success) {
    return false;
  }
  gneiss::engine::meta::property_info field{.id = gneiss::engine::meta::property_id{99},
                                            .name = "旧字段"};
  if (gneiss::engine::meta::describe_property(registry, native_id,
                                              gneiss::engine::meta::property_id{1},
                                              field) != gneiss::engine::result::not_ready ||
      field.name != "旧字段") {
    return false;
  }
  gneiss::engine::value snapshot{.payload = std::string{"旧输出"}};
  if (gneiss::engine::meta::read_property(registry, native_id, gneiss::engine::meta::property_id{1},
                                          &text, snapshot) != gneiss::engine::result::not_ready ||
      std::get<std::string>(snapshot.payload) != "旧输出" ||
      gneiss::engine::meta::freeze(registry) != gneiss::engine::result::success) {
    return false;
  }
  if (gneiss::engine::meta::read_property(registry, native_id, gneiss::engine::meta::property_id{1},
                                          &text, snapshot)
          .failed()) {
    return false;
  }
  if (gneiss::engine::meta::describe_property(registry, native_id,
                                              gneiss::engine::meta::property_id{1}, field)
          .failed() ||
      field.name != "text" || field.id != gneiss::engine::meta::property_id{1} ||
      field.kind != gneiss::engine::value_kind::string ||
      !gneiss::engine::meta::has_flags(field.capabilities,
                                       gneiss::engine::meta::property_capabilities::writable) ||
      gneiss::engine::meta::describe_property(registry, native_id,
                                              gneiss::engine::meta::property_id{99},
                                              field) != gneiss::engine::result::not_found ||
      field.name != "text" ||
      gneiss::engine::meta::describe_property({}, native_id, gneiss::engine::meta::property_id{1},
                                              field) != gneiss::engine::result::invalid_handle ||
      field.name != "text") {
    return false;
  }
  gneiss::engine::result foreign;
  std::thread reader([&] {
    gneiss::engine::meta::property_info borrowed;
    foreign = gneiss::engine::meta::describe_property(
        registry, native_id, gneiss::engine::meta::property_id{1}, borrowed);
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
  const gneiss::engine::value invalid{.payload = 1.0F};
  if (gneiss::engine::meta::write_property(registry, native_id,
                                           gneiss::engine::meta::property_id{1}, &text,
                                           invalid) != gneiss::engine::result::invalid_argument ||
      text.size() != 2000 ||
      gneiss::engine::meta::write_property(registry, native_id,
                                           gneiss::engine::meta::property_id{1}, &text, copy)
          .failed() ||
      text != "初始文本") {
    return false;
  }
  gneiss::engine::meta::type_info metadata;
  if (gneiss::engine::meta::describe_type(registry, native_id, metadata).failed() ||
      metadata.properties.size() != 1 ||
      metadata.properties[0].kind != gneiss::engine::value_kind::string ||
      metadata.guid != native_id || metadata.properties[0].type_guid != native_id ||
      gneiss::engine::meta::describe_type(registry, gneiss::engine::guid{}, metadata) !=
          gneiss::engine::result::not_found ||
      metadata.guid != native_id || metadata.properties.size() != 1 ||
      gneiss::engine::meta::describe_type({}, native_id, metadata) !=
          gneiss::engine::result::invalid_handle ||
      metadata.guid != native_id || metadata.properties.size() != 1) {
    return false;
  }
  if (gneiss::engine::meta::describe_type_at(registry, 0, metadata).failed() ||
      metadata.guid != native_id || metadata.properties.size() != 1 ||
      gneiss::engine::meta::describe_type_at(registry, 1, metadata) !=
          gneiss::engine::result::not_found ||
      metadata.guid != native_id ||
      gneiss::engine::meta::describe_type_at({}, 0, metadata) !=
          gneiss::engine::result::invalid_handle ||
      metadata.guid != native_id)
    return false;
  registry.reset();
  return gneiss::engine::meta::read_property(registry, native_id,
                                             gneiss::engine::meta::property_id{1}, &text,
                                             snapshot) == gneiss::engine::result::invalid_handle &&
         std::get<std::string>(snapshot.payload) == "初始文本";
}
} // namespace

int main() try {
  if (!native_values()) {
    return 30;
  }
  namespace reflection = gneiss::engine;
  // 原生对象不依赖公共句柄表；最后一个内部所有者释放后必须销毁。
  gneiss::engine::meta::registry_ref native;
  if (gneiss::engine::meta::create_registry(native) != gneiss::engine::result::success || !native) {
    return 20;
  }
  const std::weak_ptr native_lifetime = native;
  std::uint32_t native_count = 99;
  if (gneiss::engine::meta::type_count(native, native_count) != gneiss::engine::result::not_ready ||
      native_count != 99 ||
      gneiss::engine::meta::freeze(native) != gneiss::engine::result::success ||
      gneiss::engine::meta::type_count(native, native_count) != gneiss::engine::result::success ||
      native_count != 0) {
    return 21;
  }
  native.reset();
  if (!native_lifetime.expired() ||
      gneiss::engine::meta::freeze(native) != gneiss::engine::result::invalid_handle) {
    return 22;
  }
  gneiss::engine::meta::registry_ref state;
  if (gneiss::engine::meta::create_registry(state).failed())
    return 1;
  const gneiss::engine::guid type_id{gneiss::engine::guid::from_bytes({1})};
  std::string name = "sample";
  const std::array fields{
      gneiss::engine::meta::property_definition{
          .id = gneiss::engine::meta::property_id{1U},
          .type_guid = type_id,
          .flags = gneiss::engine::meta::property_flags::none,
          .name = "value",
      },
      gneiss::engine::meta::property_definition{.id = gneiss::engine::meta::property_id{2U},
                                                .type_guid = type_id,
                                                .flags = gneiss::engine::meta::property_flags::none,
                                                .name = "candidate"},
  };
  const gneiss::engine::meta::type_definition definition{
      .guid = type_id,
      .schema_version = 1U,
      .name = name,
      .properties = fields,
  };
  if (gneiss::engine::meta::register_type(state, definition) != gneiss::engine::result::success ||
      gneiss::engine::meta::bind_property(state, type_id, gneiss::engine::meta::property_id{1U},
                                          {.kind = gneiss::engine::value_kind::float32,
                                           .getter = throwing_getter,
                                           .setter = nullptr}) != gneiss::engine::result::success) {
    return 2;
  }
  const gneiss::engine::meta::property_binding candidate_binding{
      .kind = gneiss::engine::value_kind::float32,
      .getter = {},
      .setter = set_object,
      .user_data = {}};
  float candidate = 1;
  gneiss::engine::value proposed = gneiss::engine::value{};
  proposed.payload = 2.0F;
  if (gneiss::engine::meta::bind_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                          candidate_binding) != gneiss::engine::result::success ||
      gneiss::engine::meta::bind_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                          candidate_binding) != gneiss::engine::result::success ||
      gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::engine::result::not_ready) {
    return 11;
  }
  name.assign("changed");
  if (gneiss::engine::meta::freeze(state) != gneiss::engine::result::success) {
    return 3;
  }
  gneiss::engine::meta::type_info info;
  if (gneiss::engine::meta::describe_type(state, type_id, info) !=
          gneiss::engine::result::success ||
      info.name != "sample" || info.properties.size() != 2U) {
    return 4;
  }
  if (gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{1U},
                                           &candidate,
                                           proposed) != gneiss::engine::result::unsupported ||
      gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                           nullptr,
                                           proposed) != gneiss::engine::result::invalid_argument ||
      gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::engine::result::success ||
      candidate != 2 ||
      gneiss::engine::meta::bind_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                          candidate_binding) !=
          gneiss::engine::result::invalid_state) {
    return 12;
  }
  proposed.payload = -1.0F;
  if (gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::engine::result::invalid_argument ||
      candidate != 2) {
    return 13;
  }
  proposed.payload = 99.0F;
  if (gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::engine::result::internal ||
      candidate != 2) {
    return 14;
  }
  proposed.payload = true;
  if (gneiss::engine::meta::write_property(state, type_id, gneiss::engine::meta::property_id{2U},
                                           &candidate,
                                           proposed) != gneiss::engine::result::invalid_argument ||
      candidate != 2) {
    return 15;
  }
  gneiss::engine::value value;
  if (gneiss::engine::meta::read_property(state, type_id, gneiss::engine::meta::property_id{1U},
                                          &candidate, value) != gneiss::engine::result::internal ||
      value.kind() != gneiss::engine::value_kind::invalid) {
    return 5;
  }
  gneiss::engine::result query{};
  std::uint32_t count{};
  std::thread worker([&] { query = gneiss::engine::meta::type_count(state, count); });
  worker.join();
  if (query != gneiss::engine::result::success || count != 1U) {
    return 6;
  }
  // 原生引用直接拥有 Registry，无公共句柄表；最后一个引用释放后销毁。
  const std::weak_ptr lifetime = state;
  auto retained = state;
  state.reset();
  if (lifetime.expired() || gneiss::engine::meta::type_count(retained, count).failed())
    return 7;
  retained.reset();
  return lifetime.expired() ? 0 : 8;
} catch (...) {
  return 10;
}
