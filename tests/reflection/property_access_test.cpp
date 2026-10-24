// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/c_owner.hpp"
#include "engine/values/reflection.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace gneiss;

namespace {

constexpr gneiss_type_id camera_type{{0x21, 0x6a, 0x90, 0x31, 0x45, 0xe4, 0x47, 0x41, 0xa5, 0xd2,
                                      0xb8, 0x11, 0x6d, 0x1e, 0x61, 0x42}};
constexpr gneiss_type_id float_type{{0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
constexpr gneiss_type_id string_type{{0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}};

struct camera_fixture {
  float field_of_view = 60.0F;
  const char* label = "主相机";
  std::uint32_t label_length = 9U;
  bool fail_get = false;
  bool fail_set = false;
};

gneiss_result get_field_of_view(void* user_data, gneiss_property_target target,
                                gneiss_property_value* output) {
  if (target.context != 7U || target.object != 11U) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto* fixture = static_cast<const camera_fixture*>(user_data);
  if (fixture->fail_get) {
    throw std::runtime_error("getter probe");
  }
  output->kind = GNEISS_PROPERTY_KIND_FLOAT32;
  output->payload.float32_value = fixture->field_of_view;
  return GNEISS_SUCCESS;
}

gneiss_result set_field_of_view(void* user_data, gneiss_property_target target,
                                const gneiss_property_value* value) {
  if (target.context != 7U || target.object != 11U) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  if (value->payload.float32_value < 1.0F || value->payload.float32_value > 179.0F) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto* fixture = static_cast<camera_fixture*>(user_data);
  if (fixture->fail_set) {
    throw std::runtime_error("setter probe");
  }
  fixture->field_of_view = value->payload.float32_value;
  return GNEISS_SUCCESS;
}

gneiss_result get_label(void* user_data, gneiss_property_target /*target*/,
                        gneiss_property_value* output) {
  const auto* fixture = static_cast<const camera_fixture*>(user_data);
  output->kind = GNEISS_PROPERTY_KIND_STRING;
  output->payload.string_value = {.data = fixture->label, .length = fixture->label_length};
  return GNEISS_SUCCESS;
}

gneiss_result get_malformed(void* /*user_data*/, gneiss_property_target /*target*/,
                            gneiss_property_value* output) {
  output->kind = GNEISS_PROPERTY_KIND_FLOAT32;
  output->payload.float32_value = std::numeric_limits<float>::infinity();
  return GNEISS_SUCCESS;
}

int verify_callback_contract(gneiss::test_support::registry_owner& registry,
                             camera_fixture& fixture, gneiss_property_target target) {
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  // 不合约的 C++ 回调异常必须在适配器处转换；getter 失败清空输出。
  fixture.fail_get = true;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(camera_type, 1U, target, value) != gneiss::result::internal ||
      value.kind != GNEISS_PROPERTY_KIND_INVALID) {
    return 10;
  }
  fixture.fail_get = false;
  fixture.fail_set = true;
  value.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  value.payload.float32_value = 100.0F;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(camera_type, 1U, target, value) != gneiss::result::internal ||
      std::abs(fixture.field_of_view - 90.0F) > 0.001F) {
    return 11;
  }
  // 字符串需由调用方复制以便长期保存。
  if (gneiss::result{
          gneiss_type_registry_get_property(registry.get(), camera_type, 2U, target, &value)}
          .failed()) {
    return 12;
  }
  const std::string copied(value.payload.string_value.data, value.payload.string_value.length);
  fixture.label = "changed";
  fixture.label_length = 7U;
  if (gneiss::result{
          gneiss_type_registry_get_property(registry.get(), camera_type, 2U, target, &value)}
          .failed() ||
      copied != "主相机" ||
      std::string_view(value.payload.string_value.data, value.payload.string_value.length) !=
          "changed" ||
      registry.reset().failed() ||
      gneiss::result{gneiss_type_registry_get_property(registry.get(), camera_type, 1U, target,
                                                       &value)} != gneiss::result::invalid_handle) {
    return 13;
  }
  return 0;
}

} // namespace

int main() try {
  camera_fixture fixture;
  const std::array fields{gneiss_field_desc{.struct_size = sizeof(gneiss_field_desc),
                                            .id = 1U,
                                            .value_type_id = float_type,
                                            .flags = 0U,
                                            .name = "field_of_view",
                                            .name_length = 13U},
                          gneiss_field_desc{.struct_size = sizeof(gneiss_field_desc),
                                            .id = 2U,
                                            .value_type_id = string_type,
                                            .flags = GNEISS_FIELD_FLAG_READ_ONLY,
                                            .name = "label",
                                            .name_length = 5U},
                          gneiss_field_desc{.struct_size = sizeof(gneiss_field_desc),
                                            .id = 3U,
                                            .value_type_id = float_type,
                                            .flags = 0U,
                                            .name = "unbound",
                                            .name_length = 7U},
                          gneiss_field_desc{.struct_size = sizeof(gneiss_field_desc),
                                            .id = 4U,
                                            .value_type_id = float_type,
                                            .flags = 0U,
                                            .name = "malformed",
                                            .name_length = 9U}};
  const gneiss_type_desc type{.struct_size = sizeof(gneiss_type_desc),
                              .id = camera_type,
                              .schema_version = 1U,
                              .name = "camera",
                              .name_length = 6U,
                              .fields = fields.data(),
                              .field_count = static_cast<std::uint32_t>(fields.size())};
  gneiss::test_support::registry_owner registry;
  if (gneiss::result{gneiss_type_registry_create(registry.put())} != gneiss::result::success ||
      ([&](const gneiss_type_desc& desc) {
        return from_native(gneiss_type_registry_register(registry.get(), &desc));
      })(type) != gneiss::result::success) {
    return 1;
  }

  const gneiss_property_accessor_desc field_of_view{.struct_size =
                                                        sizeof(gneiss_property_accessor_desc),
                                                    .kind = GNEISS_PROPERTY_KIND_FLOAT32,
                                                    .getter = get_field_of_view,
                                                    .setter = set_field_of_view,
                                                    .user_data = &fixture};
  const gneiss_property_accessor_desc label{.struct_size = sizeof(gneiss_property_accessor_desc),
                                            .kind = GNEISS_PROPERTY_KIND_STRING,
                                            .getter = get_label,
                                            .setter = nullptr,
                                            .user_data = &fixture};
  const gneiss_property_accessor_desc invalid_read_only{.struct_size =
                                                            sizeof(gneiss_property_accessor_desc),
                                                        .kind = GNEISS_PROPERTY_KIND_STRING,
                                                        .getter = get_label,
                                                        .setter = set_field_of_view,
                                                        .user_data = &fixture};
  const gneiss_property_accessor_desc malformed{.struct_size =
                                                    sizeof(gneiss_property_accessor_desc),
                                                .kind = GNEISS_PROPERTY_KIND_FLOAT32,
                                                .getter = get_malformed,
                                                .setter = nullptr,
                                                .user_data = nullptr};
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id,
           const gneiss_property_accessor_desc& desc) {
        return from_native(
            gneiss_type_registry_bind_property(registry.get(), type_id, field_id, &desc));
      })(camera_type, 1U, field_of_view) != gneiss::result::success ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id,
           const gneiss_property_accessor_desc& desc) {
        return from_native(
            gneiss_type_registry_bind_property(registry.get(), type_id, field_id, &desc));
      })(camera_type, 1U, field_of_view) != gneiss::result::success ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id,
           const gneiss_property_accessor_desc& desc) {
        return from_native(
            gneiss_type_registry_bind_property(registry.get(), type_id, field_id, &desc));
      })(camera_type, 2U, invalid_read_only) != gneiss::result::invalid_argument ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id,
           const gneiss_property_accessor_desc& desc) {
        return from_native(
            gneiss_type_registry_bind_property(registry.get(), type_id, field_id, &desc));
      })(camera_type, 2U, label) != gneiss::result::success ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id,
           const gneiss_property_accessor_desc& desc) {
        return from_native(
            gneiss_type_registry_bind_property(registry.get(), type_id, field_id, &desc));
      })(camera_type, 4U, malformed) != gneiss::result::success ||
      ([&]() { return from_native(gneiss_type_registry_freeze(registry.get())); })() !=
          gneiss::result::success ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id,
           const gneiss_property_accessor_desc& desc) {
        return from_native(
            gneiss_type_registry_bind_property(registry.get(), type_id, field_id, &desc));
      })(camera_type, 3U, field_of_view) != gneiss::result::invalid_state) {
    return 2;
  }

  gneiss_field_info info{};
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_field_info& output) {
        output = GNEISS_FIELD_INFO_INIT;
        return from_native(
            gneiss_type_registry_find_field(registry.get(), type_id, field_id, &output));
      })(camera_type, 1U, info) != gneiss::result::success ||
      info.property_kind != GNEISS_PROPERTY_KIND_FLOAT32 ||
      info.property_capabilities !=
          (GNEISS_PROPERTY_CAPABILITY_READABLE | GNEISS_PROPERTY_CAPABILITY_WRITABLE)) {
    return 3;
  }

  constexpr gneiss_property_target target{.context = 7U, .object = 11U};
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(camera_type, 1U, target, value) != gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_FLOAT32 ||
      std::abs(value.payload.float32_value - 60.0F) > 0.001F) {
    return 4;
  }
  value.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  value.payload.float32_value = 90.0F;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(camera_type, 1U, target, value) != gneiss::result::success ||
      std::abs(fixture.field_of_view - 90.0F) > 0.001F) {
    return 5;
  }
  value.payload.float32_value = 200.0F;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(camera_type, 1U, target, value) != gneiss::result::invalid_argument ||
      std::abs(fixture.field_of_view - 90.0F) > 0.001F) {
    return 6;
  }
  value.payload.float32_value = std::numeric_limits<float>::quiet_NaN();
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(camera_type, 1U, target, value) != gneiss::result::invalid_argument ||
      std::abs(fixture.field_of_view - 90.0F) > 0.001F) {
    return 7;
  }
  value.kind = GNEISS_PROPERTY_KIND_UINT64;
  value.payload.uint64_value = 90U;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(camera_type, 1U, target, value) != gneiss::result::invalid_argument) {
    return 8;
  }
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(camera_type, 2U, target, value) != gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_STRING || value.payload.string_value.length != 9U ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(camera_type, 2U, target, value) != gneiss::result::unsupported ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(camera_type, 3U, target, value) != gneiss::result::unsupported ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(camera_type, 4U, target, value) != gneiss::result::internal ||
      value.kind != GNEISS_PROPERTY_KIND_INVALID) {
    return 9;
  }
  return verify_callback_contract(registry, fixture, target);
} catch (...) {
  return 99;
}
