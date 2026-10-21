// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/draw_description.hpp"
#include "engine/api/material_description.hpp"
#include "engine/api/mesh_description.hpp"
#include "engine/api/texture_description.hpp"
#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"

#include <gneiss/engine/render.h>

namespace {
using gneiss::runtime::find_application;
using gneiss::runtime::validate_application;
} // namespace

extern "C" gneiss_result gneiss_mesh_create(gneiss_application application,
                                            const gneiss_mesh_desc* desc, gneiss_mesh* out_mesh) {
  if (out_mesh == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_mesh = GNEISS_NULL_MESH;
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    gneiss::engine::resource::mesh_data value;
    const auto converted = gneiss::api::read_mesh_description(*desc, value);
    return converted == GNEISS_SUCCESS ? state->resources().create_mesh(std::move(value), out_mesh)
                                       : converted;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数均为不透明句柄，名称区分语义。
extern "C" gneiss_result gneiss_mesh_destroy(gneiss_application application, gneiss_mesh mesh) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().destroy_mesh(mesh)
                                               : validation_result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_material_create(gneiss_application application,
                                                const gneiss_material_desc* desc,
                                                gneiss_material* out_material) {
  if (out_material == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_material = GNEISS_NULL_MATERIAL;
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    gneiss::engine::render::material_resource value;
    const auto converted = gneiss::api::read_material_description(*desc, value);
    return converted == GNEISS_SUCCESS ? state->resources().create_material(value, out_material)
                                       : converted;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数均为不透明句柄，名称区分语义。
extern "C" gneiss_result gneiss_material_destroy(gneiss_application application,
                                                 gneiss_material material) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().destroy_material(material)
                                               : validation_result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_texture_create(gneiss_application application,
                                               const gneiss_texture_desc* desc,
                                               gneiss_texture* out_texture) {
  if (out_texture == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_texture = GNEISS_NULL_TEXTURE;
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    gneiss::engine::render::texture_view value;
    const auto converted = gneiss::api::read_texture_description(*desc, value);
    return converted == GNEISS_SUCCESS ? state->resources().create_texture(value, out_texture)
                                       : converted;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数均为不透明句柄，名称区分语义。
extern "C" gneiss_result gneiss_texture_destroy(gneiss_application application,
                                                gneiss_texture texture) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().destroy_texture(texture)
                                               : validation_result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_application_submit_ui_draw_list(gneiss_application application,
                                       const gneiss_ui_draw_list_desc* desc) {
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    if (!state->can_submit_draw_lists()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    gneiss::api::converted_ui_draw value;
    const auto converted = gneiss::api::read_ui_draw_description(*desc, value);
    return converted == GNEISS_SUCCESS ? state->submit_ui_draw_list(value.view) : converted;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_application_submit_debug_draw_list(gneiss_application application,
                                          const gneiss_debug_draw_list_desc* desc) {
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    if (!state->can_submit_draw_lists()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    std::vector<gneiss::engine::render::debug_line> value;
    const auto converted = gneiss::api::read_debug_draw_description(*desc, value);
    return converted == GNEISS_SUCCESS ? state->submit_debug_draw_list(value) : converted;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
