// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/render.h>

_Static_assert(sizeof(gneiss_texture_format) == sizeof(uint32_t), "Texture 格式类型必须保持 32 位");
_Static_assert(sizeof(gneiss_texture_color_space) == sizeof(uint32_t),
               "Texture 颜色空间类型必须保持 32 位");

int main(void) {
  const gneiss_mesh_vertex vertices[] = {{-0.5F, -0.5F, 0.0F, 0.0F, 0.0F},
                                         {0.5F, -0.5F, 0.0F, 1.0F, 0.0F},
                                         {0.0F, 0.5F, 0.0F, 0.5F, 1.0F}};
  const uint32_t indices[] = {0U, 1U, 2U};
  gneiss_mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  gneiss_material_desc material_desc = GNEISS_MATERIAL_DESC_INIT;
  gneiss_application_desc application_desc = GNEISS_APPLICATION_DESC_INIT;
  gneiss_application first = GNEISS_NULL_APPLICATION;
  gneiss_application second = GNEISS_NULL_APPLICATION;
  gneiss_mesh mesh = GNEISS_NULL_MESH;
  gneiss_material material = GNEISS_NULL_MATERIAL;
  gneiss_texture texture = GNEISS_NULL_TEXTURE;
  const uint8_t pixels[] = {255U, 0U, 0U, 255U};
  gneiss_texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  mesh_desc.vertices = vertices;
  mesh_desc.vertex_count = 3U;
  mesh_desc.indices = indices;
  mesh_desc.index_count = 3U;
  material_desc.red = 0.25F;
  material_desc.green = 0.5F;
  material_desc.blue = 0.75F;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixel_data_size = sizeof(pixels);
  texture_desc.pixels = pixels;

  if (gneiss_application_create(&application_desc, &first) != GNEISS_SUCCESS ||
      gneiss_application_create(&application_desc, &second) != GNEISS_SUCCESS ||
      gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_SUCCESS ||
      gneiss_material_create(first, &material_desc, &material) != GNEISS_SUCCESS ||
      gneiss_texture_create(first, &texture_desc, &texture) != GNEISS_SUCCESS ||
      mesh == GNEISS_NULL_MESH || material == GNEISS_NULL_MATERIAL ||
      texture == GNEISS_NULL_TEXTURE) {
    return 1;
  }
  if (gneiss_mesh_destroy(first, (gneiss_mesh)material) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_mesh_destroy(second, mesh) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_material_destroy(second, material) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_texture_destroy(first, (gneiss_texture)material) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_texture_destroy(second, texture) != GNEISS_ERROR_INVALID_HANDLE) {
    return 2;
  }
  if (gneiss_mesh_destroy(first, mesh) != GNEISS_SUCCESS ||
      gneiss_mesh_destroy(first, mesh) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_material_destroy(first, material) != GNEISS_SUCCESS ||
      gneiss_material_destroy(first, material) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_texture_destroy(first, texture) != GNEISS_SUCCESS ||
      gneiss_texture_destroy(first, texture) != GNEISS_ERROR_INVALID_HANDLE) {
    return 3;
  }
  mesh_desc.vertex_count = 2U;
  material_desc.alpha = 2.0F;
  texture_desc.row_stride_bytes = 3U;
  if (gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_material_create(first, &material_desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_texture_create(first, &texture_desc, &texture) != GNEISS_ERROR_INVALID_ARGUMENT ||
      texture != GNEISS_NULL_TEXTURE) {
    return 4;
  }
  /* 真实 C 入口只读取已声明的旧版字段，尾部非法值不影响旧调用方。 */
  material_desc = (gneiss_material_desc)GNEISS_MATERIAL_DESC_INIT;
  material_desc.struct_size = GNEISS_MATERIAL_DESC_VERSION_1_SIZE;
  material_desc.normal_texture = UINT64_MAX;
  material_desc.alpha_mode = UINT32_MAX;
  material_desc.reserved_2 = 1U;
  if (gneiss_material_create(first, &material_desc, &material) != GNEISS_SUCCESS ||
      gneiss_material_destroy(first, material) != GNEISS_SUCCESS) {
    return 6;
  }
  material_desc.struct_size = GNEISS_MATERIAL_DESC_VERSION_2_SIZE;
  material_desc.normal_texture = GNEISS_NULL_TEXTURE;
  material_desc.reserved_2 = 0U;
  material_desc.reserved_3 = 1U;
  if (gneiss_material_create(first, &material_desc, &material) != GNEISS_SUCCESS ||
      gneiss_material_destroy(first, material) != GNEISS_SUCCESS) {
    return 7;
  }
  material_desc.struct_size = GNEISS_MATERIAL_DESC_VERSION_1_SIZE + 1U;
  material = UINT64_C(1);
  if (gneiss_material_create(first, &material_desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT ||
      material != GNEISS_NULL_MATERIAL) {
    return 8;
  }
  material_desc = (gneiss_material_desc)GNEISS_MATERIAL_DESC_INIT;
  material_desc.reserved_3 = 1U;
  material = UINT64_C(1);
  if (gneiss_material_create(first, &material_desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT ||
      material != GNEISS_NULL_MATERIAL) {
    return 9;
  }
  mesh_desc = (gneiss_mesh_desc)GNEISS_MESH_DESC_INIT;
  mesh_desc.vertices = vertices;
  mesh_desc.vertex_count = 3U;
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_1_SIZE;
  mesh_desc.reserved_4 = 1U;
  mesh_desc.tangent_count = UINT32_MAX;
  if (gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_SUCCESS ||
      gneiss_mesh_destroy(first, mesh) != GNEISS_SUCCESS) {
    return 10;
  }
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_2_SIZE;
  mesh_desc.reserved_4 = 0U;
  mesh_desc.tangent_count = 0U;
  mesh_desc.reserved_5 = 1U;
  mesh_desc.uv1_count = UINT32_MAX;
  if (gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_SUCCESS ||
      gneiss_mesh_destroy(first, mesh) != GNEISS_SUCCESS) {
    return 11;
  }
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_1_SIZE + 1U;
  mesh = UINT64_C(1);
  if (gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT ||
      mesh != GNEISS_NULL_MESH) {
    return 12;
  }
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_1_SIZE;
  mesh_desc.index_count = 1U;
  if (gneiss_mesh_create(first, &mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT ||
      mesh != GNEISS_NULL_MESH) {
    return 13;
  }
  texture_desc.row_stride_bytes = 4U;
  texture_desc.reserved[1] = 1U;
  texture = UINT64_C(1);
  if (gneiss_texture_create(first, &texture_desc, &texture) != GNEISS_ERROR_INVALID_ARGUMENT ||
      texture != GNEISS_NULL_TEXTURE) {
    return 14;
  }
  texture_desc.reserved[1] = 0U;
  texture_desc.struct_size = sizeof(gneiss_texture_desc) - 1U;
  texture = UINT64_C(1);
  if (gneiss_texture_create(first, &texture_desc, &texture) != GNEISS_ERROR_INVALID_ARGUMENT ||
      texture != GNEISS_NULL_TEXTURE) {
    return 15;
  }
#if SIZE_MAX < UINT64_MAX
  texture_desc.struct_size = sizeof(gneiss_texture_desc);
  texture_desc.pixel_data_size = (uint64_t)SIZE_MAX + UINT64_C(1);
  texture = UINT64_C(1);
  if (gneiss_texture_create(first, &texture_desc, &texture) != GNEISS_ERROR_INVALID_ARGUMENT ||
      texture != GNEISS_NULL_TEXTURE) {
    return 16;
  }
#endif
  if (gneiss_application_destroy(second) != GNEISS_SUCCESS ||
      gneiss_application_destroy(first) != GNEISS_SUCCESS) {
    return 5;
  }
  return 0;
}
