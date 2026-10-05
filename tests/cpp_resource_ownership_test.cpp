// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>

#include <array>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
template <typename Resource, typename Desc, auto Destroy>
bool verify_owner(gneiss::application& app, const Desc& desc) {
  static_assert(!std::is_copy_constructible_v<Resource>);
  static_assert(std::is_nothrow_move_constructible_v<Resource>);
  Resource first;
  if (Resource::create(app.get(), desc, first).failed()) {
    return false;
  }
  const auto original = first.get();
  auto invalid = desc;
  invalid.struct_size = 0U;
  if (Resource::create(app.get(), invalid, first) != gneiss::result::invalid_argument ||
      first.get() != original) {
    return false;
  }
  Resource transferred{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): 验证移动后源包装清空的契约。
  if (first || transferred.get() != original) {
    return false;
  }
  Resource second;
  if (Resource::create(app.get(), desc, second).failed()) {
    return false;
  }
  const auto replaced = second.get();
  second = std::move(transferred);
  // NOLINTNEXTLINE(bugprone-use-after-move): 验证移动赋值后源包装清空的契约。
  if (transferred || second.get() != original ||
      Destroy(app.get(), replaced) != GNEISS_ERROR_INVALID_HANDLE) {
    return false;
  }
  gneiss::result wrong_thread;
  std::thread worker([&] { wrong_thread = second.reset(); });
  worker.join();
  if (wrong_thread != gneiss::result::invalid_state || second.get() != original) {
    return false;
  }
  const auto owner = second.owner();
  const auto released = second.release();
  if (second || owner != app.get() || released.get() != original ||
      Destroy(owner, released.get()) != GNEISS_SUCCESS || second.reset().failed()) {
    return false;
  }
  std::uint64_t scoped{};
  {
    Resource temporary;
    if (Resource::create(app.get(), desc, temporary).failed()) {
      return false;
    }
    scoped = temporary.get();
  }
  return Destroy(app.get(), scoped) == GNEISS_ERROR_INVALID_HANDLE;
}
} // namespace

int main() {
  gneiss::application app;
  const gneiss_application_desc app_desc = GNEISS_APPLICATION_DESC_INIT;
  if (gneiss::application::create(app_desc, app).failed()) {
    return 1;
  }
  const std::array vertices{
      gneiss_mesh_vertex{.x = 0, .y = 0, .z = 0, .u = 0, .v = 0},
      gneiss_mesh_vertex{.x = 1, .y = 0, .z = 0, .u = 1, .v = 0},
      gneiss_mesh_vertex{.x = 0, .y = 1, .z = 0, .u = 0, .v = 1},
  };
  gneiss::mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  mesh_desc.vertices = vertices.data();
  mesh_desc.vertex_count = 3U;
  gneiss::material_desc material_desc = GNEISS_MATERIAL_DESC_INIT;
  const std::array<std::uint8_t, 4> pixels{255, 255, 255, 255};
  gneiss::texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixel_data_size = pixels.size();
  texture_desc.pixels = pixels.data();
  if (!verify_owner<gneiss::mesh, gneiss::mesh_desc, gneiss_mesh_destroy>(app, mesh_desc) ||
      !verify_owner<gneiss::material, gneiss::material_desc, gneiss_material_destroy>(
          app, material_desc) ||
      !verify_owner<gneiss::texture, gneiss::texture_desc, gneiss_texture_destroy>(app,
                                                                                   texture_desc)) {
    return 2;
  }
  gneiss::world_ref borrowed;
  if (app.get_world(borrowed).failed()) {
    return 3;
  }
  std::uint64_t count{};
  {
    const auto copy = borrowed;
    if (copy.entity_count(count).failed()) {
      return 4;
    }
  }
  if (borrowed.entity_count(count).failed()) {
    return 5;
  }
  gneiss::mesh mesh;
  gneiss::material material;
  gneiss::texture texture;
  if (app.create_mesh(mesh_desc, mesh).failed() ||
      app.create_material(material_desc, material).failed() ||
      app.create_texture(texture_desc, texture).failed()) {
    return 6;
  }
  gneiss::entity_id rendered;
  const gneiss::mesh_renderer renderer{.mesh = mesh.get(), .material = material.get()};
  const gneiss::mesh_renderer invalid = GNEISS_MESH_RENDERER_INIT;
  if (borrowed.create_entity(rendered).failed() ||
      borrowed.set_mesh_renderer(rendered, invalid) != gneiss::result::invalid_argument ||
      borrowed.set_mesh_renderer(rendered, renderer).failed() ||
      borrowed.remove_mesh_renderer(rendered).failed() ||
      borrowed.remove_mesh_renderer(rendered) != gneiss::result::not_found ||
      borrowed.set_mesh_renderer(rendered, renderer).failed() ||
      borrowed.destroy_entity(rendered).failed() ||
      borrowed.set_mesh_renderer(rendered, renderer) != gneiss::result::invalid_handle) {
    return 11;
  }
  // 组件只借用 RID；删除组件/实体后，原拥有者仍可释放资源。
  if (app.destroy_mesh(mesh.release()).failed() ||
      app.destroy_material(material.release()).failed() ||
      app.create_mesh(mesh_desc, mesh).failed() ||
      app.create_material(material_desc, material).failed()) {
    return 12;
  }
  app.reset();
  if (borrowed.entity_count(count) != gneiss::result::invalid_handle || mesh.reset().failed() ||
      material.reset().failed() || texture.reset().failed() || mesh || material || texture) {
    return 7;
  }
  static_assert(!std::is_convertible_v<gneiss::world*, gneiss::world_ref*>);
  gneiss::world world;
  if (gneiss::world::create(world).failed()) {
    return 8;
  }
  const auto view = world.ref();
  gneiss::world moved{std::move(world)};
  if (view.entity_count(count).failed()) {
    return 9;
  }
  moved.reset();
  return view.entity_count(count) == gneiss::result::invalid_handle ? 0 : 10;
}
