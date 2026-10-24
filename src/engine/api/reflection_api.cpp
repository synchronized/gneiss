// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include <gneiss/engine/reflection.h>
#include <new>
#include <vector>

namespace reflection = gneiss::engine;

extern "C" gneiss_result gneiss_type_registry_create(gneiss_type_registry* out_registry) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_registry == nullptr) {
        return gneiss::engine::result::invalid_argument;
      }
      return gneiss::engine::create_type_registry(*out_registry);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_destroy(gneiss_type_registry registry) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      return gneiss::engine::destroy_type_registry(registry);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_register(gneiss_type_registry registry,
                                                       const gneiss_type_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      const auto state = gneiss::engine::resolve(registry);
      if (!state) {
        return gneiss::engine::result::invalid_handle;
      }
      if (desc == nullptr || desc->struct_size < sizeof(gneiss_type_desc) ||
          desc->name == nullptr || desc->name_length == 0U ||
          (desc->field_count != 0U && desc->fields == nullptr)) {
        return gneiss::engine::result::invalid_argument;
      }
      for (std::uint32_t index = 0; index < desc->field_count; ++index) {
        const auto& field = desc->fields[index];
        if (field.struct_size < sizeof(gneiss_field_desc) || field.name == nullptr ||
            field.name_length == 0U) {
          return gneiss::engine::result::invalid_argument;
        }
      }
      std::vector<gneiss::engine::field_definition> fields;
      fields.reserve(desc->field_count);
      for (std::uint32_t index = 0; index < desc->field_count; ++index) {
        const auto& field = desc->fields[index];
        fields.push_back({
            .id = field.id,
            .value_type_id = field.value_type_id,
            .flags = field.flags,
            .name = {field.name, field.name_length},
        });
      }
      return gneiss::engine::register_type(state, {
                                                      .id = desc->id,
                                                      .schema_version = desc->schema_version,
                                                      .name = {desc->name, desc->name_length},
                                                      .fields = fields,
                                                  });
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_type_registry_bind_property(gneiss_type_registry registry, gneiss_type_id type_id,
                                   gneiss_field_id field_id,
                                   const gneiss_property_accessor_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (!gneiss::engine::valid_type_id(type_id) || field_id == GNEISS_NULL_FIELD_ID ||
          desc == nullptr) {
        return gneiss::engine::result::invalid_argument;
      }
      const auto state = gneiss::engine::resolve(registry);
      if (state && desc->struct_size < sizeof(gneiss_property_accessor_desc)) {
        return gneiss::engine::result::invalid_argument;
      }
      if (!state)
        return gneiss::engine::result::invalid_handle;
      gneiss::engine::property_binding binding{};
      binding.kind = desc->kind;
      binding.getter_identity = desc->getter;
      binding.setter_identity = desc->setter;
      binding.user_data = desc->user_data;
      if (desc->getter) {
        binding.getter = [fn = desc->getter](void* data, gneiss_property_target target,
                                             gneiss_property_value* output) {
          return gneiss::engine::detail::from_c_result(fn(data, target, output));
        };
      }
      if (desc->setter) {
        binding.setter = [fn = desc->setter](void* data, gneiss_property_target target,
                                             const gneiss_property_value* value) {
          return gneiss::engine::detail::from_c_result(fn(data, target, value));
        };
      }
      return gneiss::engine::bind_property(state, type_id, field_id, binding);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_freeze(gneiss_type_registry registry) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr ? gneiss::engine::result::invalid_handle
                              : gneiss::engine::freeze(state);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_is_frozen(gneiss_type_registry registry,
                                                        uint8_t* out_is_frozen) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_is_frozen == nullptr) {
        return gneiss::engine::result::invalid_argument;
      }
      *out_is_frozen = UINT8_C(0);
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr ? gneiss::engine::result::invalid_handle
                              : gneiss::engine::is_frozen(state, *out_is_frozen);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_type_count(gneiss_type_registry registry,
                                                         uint32_t* out_count) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_count == nullptr) {
        return gneiss::engine::result::invalid_argument;
      }
      *out_count = 0U;
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr ? gneiss::engine::result::invalid_handle
                              : gneiss::engine::type_count(state, *out_count);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 的 Registry、索引顺序固定。
extern "C" gneiss_result gneiss_type_registry_type_at(gneiss_type_registry registry, uint32_t index,
                                                      gneiss_type_info* out_type) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_type == nullptr || out_type->struct_size < GNEISS_TYPE_INFO_VERSION_1_SIZE) {
        return gneiss::engine::result::invalid_argument;
      }
      *out_type = GNEISS_TYPE_INFO_INIT;
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr ? gneiss::engine::result::invalid_handle
                              : gneiss::engine::type_at(state, index, *out_type);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_find_type(gneiss_type_registry registry,
                                                        gneiss_type_id id,
                                                        gneiss_type_info* out_type) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_type == nullptr || out_type->struct_size < GNEISS_TYPE_INFO_VERSION_1_SIZE ||
          !gneiss::engine::valid_type_id(id)) {
        return gneiss::engine::result::invalid_argument;
      }
      *out_type = GNEISS_TYPE_INFO_INIT;
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr ? gneiss::engine::result::invalid_handle
                              : gneiss::engine::find_type(state, id, *out_type);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_find_field(gneiss_type_registry registry,
                                                         gneiss_type_id type_id,
                                                         gneiss_field_id field_id,
                                                         gneiss_field_info* out_field) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_field == nullptr || out_field->struct_size < GNEISS_FIELD_INFO_VERSION_1_SIZE ||
          !gneiss::engine::valid_type_id(type_id) || field_id == GNEISS_NULL_FIELD_ID) {
        return gneiss::engine::result::invalid_argument;
      }
      *out_field = GNEISS_FIELD_INFO_INIT;
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr ? gneiss::engine::result::invalid_handle
                              : gneiss::engine::find_field(state, type_id, field_id, *out_field);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_get_property(gneiss_type_registry registry,
                                                           gneiss_type_id type_id,
                                                           gneiss_field_id field_id,
                                                           gneiss_property_target target,
                                                           gneiss_property_value* out_value) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (out_value == nullptr || !gneiss::engine::valid_type_id(type_id) ||
          field_id == GNEISS_NULL_FIELD_ID) {
        return gneiss::engine::result::invalid_argument;
      }
      *out_value = GNEISS_PROPERTY_VALUE_INIT;
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : gneiss::engine::get_property(state, type_id, field_id, target, *out_value);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_type_registry_set_property(gneiss_type_registry registry,
                                                           gneiss_type_id type_id,
                                                           gneiss_field_id field_id,
                                                           gneiss_property_target target,
                                                           const gneiss_property_value* value) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      if (value == nullptr || !gneiss::engine::valid_type_id(type_id) ||
          field_id == GNEISS_NULL_FIELD_ID) {
        return gneiss::engine::result::invalid_argument;
      }
      const auto state = gneiss::engine::resolve(registry);
      return state == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : gneiss::engine::set_property(state, type_id, field_id, target, *value);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}
