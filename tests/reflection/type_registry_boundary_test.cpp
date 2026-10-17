// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
int runtime_writes = 0;
gneiss_result set_runtime(void*, gneiss_property_target, const gneiss_property_value*) {
  ++runtime_writes;
  return GNEISS_SUCCESS;
}
gneiss_result set_candidate(void*, void* target, const gneiss_property_value& value) {
  if (value.payload.float32_value < 0) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (value.payload.float32_value == 99) {
    throw std::runtime_error("候选校验异常");
  }
  *static_cast<float*>(target) = value.payload.float32_value;
  return GNEISS_SUCCESS;
}
gneiss_result throwing_getter(void* /*context*/, gneiss_property_target /*target*/,
                              gneiss_property_value* /*output*/) {
  throw std::runtime_error("test callback failure");
}
} // namespace

namespace {
bool native_values() {
  namespace reflection = gneiss::reflection_internal;
  reflection::registry_ref registry;
  const gneiss_type_id id{{7}};
  const gneiss::type_id native_id{{7}};
  const std::array fields{
      reflection::field_definition{.id = 1, .value_type_id = id, .flags = 0, .name = "text"}};
  std::string text = "初始文本";
  const reflection::property_binding binding{
      .kind = GNEISS_PROPERTY_KIND_STRING,
      .getter =
          [](void* context, gneiss_property_target, gneiss_property_value* output) {
            const auto& value = *static_cast<std::string*>(context);
            *output = GNEISS_PROPERTY_VALUE_INIT;
            output->kind = GNEISS_PROPERTY_KIND_STRING;
            output->payload.string_value = {value.data(), static_cast<std::uint32_t>(value.size())};
            return GNEISS_SUCCESS;
          },
      .setter =
          [](void* context, gneiss_property_target, const gneiss_property_value* value) {
            static_cast<std::string*>(context)->assign(value->payload.string_value.data,
                                                       value->payload.string_value.length);
            return GNEISS_SUCCESS;
          },
      .user_data = &text};
  if (reflection::create_native(registry) != GNEISS_SUCCESS ||
      reflection::register_type(
          registry, {.id = id, .schema_version = 1, .name = "sample", .fields = fields}) !=
          GNEISS_SUCCESS ||
      reflection::bind_property(registry, id, 1, binding) != GNEISS_SUCCESS) {
    return false;
  }
  reflection::property_value snapshot{.payload = std::string{"旧输出"}};
  if (reflection::read_property(registry, native_id, gneiss::field_id{1}, {}, snapshot) !=
          gneiss::result::not_ready ||
      std::get<std::string>(snapshot.payload) != "旧输出" ||
      reflection::freeze(registry) != GNEISS_SUCCESS) {
    return false;
  }
  if (reflection::read_property(registry, native_id, gneiss::field_id{1}, {}, snapshot).failed()) {
    return false;
  }
  text.assign(2000, 'x');
  auto copy = snapshot;
  if (std::get<std::string>(copy.payload) != "初始文本") {
    return false;
  }
  const reflection::property_value invalid{.payload = 1.0F};
  if (reflection::write_property(registry, native_id, gneiss::field_id{1}, {}, invalid) !=
          gneiss::result::invalid_argument ||
      text.size() != 2000 ||
      reflection::write_property(registry, native_id, gneiss::field_id{1}, {}, copy).failed() ||
      text != "初始文本") {
    return false;
  }
  gneiss::type_info metadata;
  if (reflection::describe_type(registry, native_id, metadata).failed() ||
      metadata.fields.size() != 1 || metadata.fields[0].kind != gneiss::property_kind::string) {
    return false;
  }
  registry.reset();
  return reflection::read_property(registry, native_id, gneiss::field_id{1}, {}, snapshot) ==
             gneiss::result::invalid_handle &&
         std::get<std::string>(snapshot.payload) == "初始文本";
}
} // namespace

int main() try {
  if (!native_values()) {
    return 30;
  }
  namespace reflection = gneiss::reflection_internal;
  // 原生对象不依赖公共句柄表；最后一个内部所有者释放后必须销毁。
  reflection::registry_ref native;
  if (reflection::create_native(native) != GNEISS_SUCCESS || !native) {
    return 20;
  }
  const std::weak_ptr native_lifetime = native;
  std::uint32_t native_count = 99;
  if (reflection::type_count(native, native_count) != GNEISS_ERROR_NOT_READY ||
      native_count != 99 || reflection::freeze(native) != GNEISS_SUCCESS ||
      reflection::type_count(native, native_count) != GNEISS_SUCCESS || native_count != 0) {
    return 21;
  }
  native.reset();
  if (!native_lifetime.expired() || reflection::freeze(native) != GNEISS_ERROR_INVALID_HANDLE) {
    return 22;
  }
  gneiss_type_registry registry{};
  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS) {
    return 1;
  }
  auto state = reflection::resolve(registry);
  const gneiss_type_id type_id{{1}};
  std::string name = "sample";
  const std::array fields{
      reflection::field_definition{
          .id = 1U,
          .value_type_id = type_id,
          .flags = 0U,
          .name = "value",
      },
      reflection::field_definition{
          .id = 2U, .value_type_id = type_id, .flags = 0U, .name = "candidate"},
  };
  const reflection::type_definition definition{
      .id = type_id,
      .schema_version = 1U,
      .name = name,
      .fields = fields,
  };
  if (reflection::register_type(state, definition) != GNEISS_SUCCESS ||
      reflection::bind_property(state, type_id, 1U,
                                {.kind = GNEISS_PROPERTY_KIND_FLOAT32,
                                 .getter = throwing_getter,
                                 .setter = set_runtime}) != GNEISS_SUCCESS) {
    return 2;
  }
  const reflection::property_binding candidate_binding{.kind = GNEISS_PROPERTY_KIND_FLOAT32,
                                                       .candidate_setter = set_candidate};
  float candidate = 1;
  gneiss_property_value proposed = GNEISS_PROPERTY_VALUE_INIT;
  proposed.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  proposed.payload.float32_value = 2;
  if (reflection::bind_property(state, type_id, 2U, candidate_binding) != GNEISS_SUCCESS ||
      reflection::bind_property(state, type_id, 2U, candidate_binding) != GNEISS_SUCCESS ||
      reflection::set_candidate_property(state, type_id, 2U, &candidate, proposed) !=
          GNEISS_ERROR_NOT_READY) {
    return 11;
  }
  name.assign("changed");
  if (gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS) {
    return 3;
  }
  gneiss_type_info info = GNEISS_TYPE_INFO_INIT;
  if (gneiss_type_registry_find_type(registry, type_id, &info) != GNEISS_SUCCESS ||
      std::string_view(info.name, info.name_length) != "sample" || info.field_count != 2U) {
    return 4;
  }
  if (reflection::set_candidate_property(state, type_id, 1U, &candidate, proposed) !=
          GNEISS_ERROR_UNSUPPORTED ||
      reflection::set_candidate_property(state, type_id, 2U, nullptr, proposed) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      reflection::set_candidate_property(state, type_id, 2U, &candidate, proposed) !=
          GNEISS_SUCCESS ||
      candidate != 2 ||
      reflection::set_property(state, type_id, 2U, {}, proposed) != GNEISS_ERROR_UNSUPPORTED ||
      reflection::bind_property(state, type_id, 2U, candidate_binding) !=
          GNEISS_ERROR_INVALID_STATE) {
    return 12;
  }
  proposed.payload.float32_value = -1;
  if (reflection::set_candidate_property(state, type_id, 2U, &candidate, proposed) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      candidate != 2) {
    return 13;
  }
  proposed.payload.float32_value = 99;
  if (reflection::set_candidate_property(state, type_id, 2U, &candidate, proposed) !=
          GNEISS_ERROR_INTERNAL ||
      candidate != 2) {
    return 14;
  }
  proposed.kind = GNEISS_PROPERTY_KIND_BOOL;
  proposed.payload.bool_value = 1;
  if (reflection::set_candidate_property(state, type_id, 2U, &candidate, proposed) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      runtime_writes != 0 || candidate != 2) {
    return 15;
  }
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  if (gneiss_type_registry_get_property(registry, type_id, 1U, {}, &value) !=
          GNEISS_ERROR_INTERNAL ||
      value.kind != GNEISS_PROPERTY_KIND_INVALID) {
    return 5;
  }
  gneiss_result query{};
  std::uint32_t count{};
  std::thread worker([&] { query = gneiss_type_registry_type_count(registry, &count); });
  worker.join();
  if (query != GNEISS_SUCCESS || count != 1U) {
    return 6;
  }
  // 公共句柄失效与内部调用持有的临时租约是两个不同的生命周期。
  const auto stale = registry;
  if (reflection::destroy(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_type_count(stale, &count) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_type_registry_register(stale, nullptr) != GNEISS_ERROR_INVALID_HANDLE) {
    return 7;
  }
  state.reset();
  if (reflection::create(registry) != GNEISS_SUCCESS || registry == stale ||
      gneiss_type_registry_register(registry, nullptr) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 8;
  }
  return gneiss_type_registry_destroy(registry) == GNEISS_SUCCESS ? 0 : 9;
} catch (...) {
  return 10;
}
