// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/render.h>

int main(void) {
  const gneiss_mesh_vertex vertices[] = {
      {-0.5F, -0.5F, 0.0F}, {0.5F, -0.5F, 0.0F}, {0.0F, 0.5F, 0.0F}};
  gneiss_mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  gneiss_material_desc material_desc = GNEISS_MATERIAL_DESC_INIT;
  gneiss_application_desc application_desc = GNEISS_APPLICATION_DESC_INIT;
  gneiss_application first = GNEISS_NULL_APPLICATION;
  gneiss_application second = GNEISS_NULL_APPLICATION;
  gneiss_mesh mesh = GNEISS_NULL_MESH;
  gneiss_material material = GNEISS_NULL_MATERIAL;
  mesh_desc.vertices = vertices;
  mesh_desc.vertex_count = 3U;
  material_desc.red = 0.25F;
  material_desc.green = 0.5F;
  material_desc.blue = 0.75F;

  if (gneiss_application_create(&application_desc, &first) != GNEISS_SUCCESS ||
      gneiss_application_create(&application_desc, &second) != GNEISS_SUCCESS ||
      gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_SUCCESS ||
      gneiss_material_create(first, &material_desc, &material) != GNEISS_SUCCESS ||
      mesh == GNEISS_NULL_MESH || material == GNEISS_NULL_MATERIAL) {
    return 1;
  }
  if (gneiss_mesh_destroy(first, (gneiss_mesh)material) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_mesh_destroy(second, mesh) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_material_destroy(second, material) != GNEISS_ERROR_INVALID_HANDLE) {
    return 2;
  }
  if (gneiss_mesh_destroy(first, mesh) != GNEISS_SUCCESS ||
      gneiss_mesh_destroy(first, mesh) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_material_destroy(first, material) != GNEISS_SUCCESS ||
      gneiss_material_destroy(first, material) != GNEISS_ERROR_INVALID_HANDLE) {
    return 3;
  }
  mesh_desc.vertex_count = 2U;
  material_desc.alpha = 2.0F;
  if (gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_material_create(first, &material_desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 4;
  }
  if (gneiss_application_destroy(second) != GNEISS_SUCCESS ||
      gneiss_application_destroy(first) != GNEISS_SUCCESS) {
    return 5;
  }
  return 0;
}
