// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "mesh_data.hpp"
#include <caors/resources/resource.h>

namespace gneiss::engine::resource {
/** 数组只属于这个资源，别名 shared_ptr 通过 CAORS handle 保活。 */
struct mesh_document final : ::caors::resource {
  using ::caors::resource::resource;
  mesh_data data;
};
}
