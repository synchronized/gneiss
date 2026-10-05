// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "reflection/type_registry.hpp"
#include <gneiss/reflection.h>
#include <new>
#include <vector>

namespace reflection = gneiss::reflection_internal;

extern "C" gneiss_result gneiss_type_registry_create(gneiss_type_registry* out_registry) {
  try {
    if (out_registry == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    return reflection::create(*out_registry);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_destroy(gneiss_type_registry registry) {
  try {
    return reflection::destroy(registry);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_register(gneiss_type_registry registry,
                                                       const gneiss_type_desc* desc) {
  try {
    const auto state = reflection::resolve(registry);
    if (!state) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    if (desc == nullptr || desc->struct_size < sizeof(gneiss_type_desc) || desc->name == nullptr ||
        desc->name_length == 0U || (desc->field_count != 0U && desc->fields == nullptr)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    for (std::uint32_t index = 0; index < desc->field_count; ++index) {
      const auto& field = desc->fields[index];
      if (field.struct_size < sizeof(gneiss_field_desc) || field.name == nullptr ||
          field.name_length == 0U) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
    }
    std::vector<reflection::field_definition> fields;
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
    return reflection::register_type(state, {
                                                .id = desc->id,
                                                .schema_version = desc->schema_version,
                                                .name = {desc->name, desc->name_length},
                                                .fields = fields,
                                            });
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_type_registry_bind_property(gneiss_type_registry registry, gneiss_type_id type_id,
                                   gneiss_field_id field_id,
                                   const gneiss_property_accessor_desc* desc) {
  try {
    if (!reflection::valid_type_id(type_id) || field_id == GNEISS_NULL_FIELD_ID ||
        desc == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto state = reflection::resolve(registry);
    if (state && desc->struct_size < sizeof(gneiss_property_accessor_desc)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::bind_property(state, type_id, field_id,
                                                        {
                                                            .kind = desc->kind,
                                                            .getter = desc->getter,
                                                            .setter = desc->setter,
                                                            .user_data = desc->user_data,
                                                        });
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_freeze(gneiss_type_registry registry) {
  try {
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE : reflection::freeze(state);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_is_frozen(gneiss_type_registry registry,
                                                        uint8_t* out_is_frozen) {
  try {
    if (out_is_frozen == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_is_frozen = UINT8_C(0);
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::is_frozen(state, *out_is_frozen);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_type_count(gneiss_type_registry registry,
                                                         uint32_t* out_count) {
  try {
    if (out_count == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_count = 0U;
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::type_count(state, *out_count);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 的 Registry、索引顺序固定。
extern "C" gneiss_result gneiss_type_registry_type_at(gneiss_type_registry registry, uint32_t index,
                                                      gneiss_type_info* out_type) {
  try {
    if (out_type == nullptr || out_type->struct_size < GNEISS_TYPE_INFO_VERSION_1_SIZE) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_type = GNEISS_TYPE_INFO_INIT;
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::type_at(state, index, *out_type);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_find_type(gneiss_type_registry registry,
                                                        gneiss_type_id id,
                                                        gneiss_type_info* out_type) {
  try {
    if (out_type == nullptr || out_type->struct_size < GNEISS_TYPE_INFO_VERSION_1_SIZE ||
        !reflection::valid_type_id(id)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_type = GNEISS_TYPE_INFO_INIT;
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::find_type(state, id, *out_type);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_find_field(gneiss_type_registry registry,
                                                         gneiss_type_id type_id,
                                                         gneiss_field_id field_id,
                                                         gneiss_field_info* out_field) {
  try {
    if (out_field == nullptr || out_field->struct_size < GNEISS_FIELD_INFO_VERSION_1_SIZE ||
        !reflection::valid_type_id(type_id) || field_id == GNEISS_NULL_FIELD_ID) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_field = GNEISS_FIELD_INFO_INIT;
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::find_field(state, type_id, field_id, *out_field);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_get_property(gneiss_type_registry registry,
                                                           gneiss_type_id type_id,
                                                           gneiss_field_id field_id,
                                                           gneiss_property_target target,
                                                           gneiss_property_value* out_value) {
  try {
    if (out_value == nullptr || !reflection::valid_type_id(type_id) ||
        field_id == GNEISS_NULL_FIELD_ID) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_value = GNEISS_PROPERTY_VALUE_INIT;
    const auto state = reflection::resolve(registry);
    return state == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : reflection::get_property(state, type_id, field_id, target, *out_value);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_type_registry_set_property(gneiss_type_registry registry,
                                                           gneiss_type_id type_id,
                                                           gneiss_field_id field_id,
                                                           gneiss_property_target target,
                                                           const gneiss_property_value* value) {
  try {
    if (value == nullptr || !reflection::valid_type_id(type_id) ||
        field_id == GNEISS_NULL_FIELD_ID) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto state = reflection::resolve(registry);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : reflection::set_property(state, type_id, field_id, target, *value);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
