// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/render/detail/scene.hpp"
#include <gneiss/engine/function/render/service.hpp>
#include <array>
#include <iostream>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      std::cerr << __LINE__ << ": " #x "\n";                                                       \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)
int main() {
  using namespace gneiss;
  auto mesh = std::make_shared<gneiss::render::detail::resource_identity<mesh_data>>();
  auto material = std::make_shared<gneiss::render::detail::resource_identity<material_data>>();
  std::array<std::shared_ptr<gneiss::render::detail::resource_identity<texture_data>>, 5> textures;
  for (auto& texture : textures)
    texture = std::make_shared<gneiss::render::detail::resource_identity<texture_data>>();
  auto scene = std::make_unique<render::scene>();
  scene->apply({{render::create_instance{{1}, {.mesh = 1, .material = 2}}, {mesh, material}}});
  render::frame_request request{.window = {.width = 640, .height = 480}, .views = {{}}};
  auto capture = [&](const render::frame_request& input) -> result<render::detail::frame_packet> {
    render::detail::frame_packet packet;
    packet.window = input.window;
    packet.ui = input.ui;
    packet.debug = input.debug;
    auto status = scene->extract(input.views.front(), {640, 480}, packet);
    if (!status)
      return status.error();
    return packet;
  };
  auto pending = capture(request);
  CHECK(pending && pending->resources.mesh_pending(1) && !pending->resources.mesh_pending(0));
  const auto* fallback = pending->resources.get_material(2);
  CHECK(fallback && !pending->resources.get_material(0) && !pending->resources.get_mesh(1));
  auto description = std::make_shared<material_data>();
  description->base_color_texture = 3;
  description->metallic_roughness_texture = 4;
  description->normal_texture = 5;
  description->occlusion_texture = 6;
  description->emissive_texture = 7;
  gneiss::render::detail::resource_identity<material_data>::dependencies dependencies;
  for (std::size_t i = 0; i < textures.size(); ++i)
    dependencies[i] = textures[i];
  material->publish(description, std::move(dependencies));
  auto waiting_textures = capture(request);
  CHECK(waiting_textures && waiting_textures->resources.texture_pending(3) &&
        !waiting_textures->resources.texture_pending(0));
  auto vertices = std::make_shared<mesh_data>();
  vertices->vertices.resize(3);
  mesh->publish(vertices);
  for (auto& texture : textures) {
    auto pixels = std::make_shared<texture_data>();
    pixels->width = pixels->height = 1;
    pixels->levels.push_back(
        {.width = 1,
         .height = 1,
         .pixels = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{255}}});
    texture->publish(pixels);
  }
  const std::array lines{debug_line{.start = {0, 0, 0},
                                    .end = {1, 0, 0},
                                    .color_rgba8 = 0xffffffff,
                                    .width = 1,
                                    .depth_test = true}};
  CHECK(request.debug.replace(lines).ok());
  gneiss::render::service resources;
  std::array<ui_vertex, 3> ui_vertices{};
  const std::array<std::uint32_t, 3> ui_indices{0, 1, 2};
  CHECK(request.ui
            .replace({.display_width = 640,
                      .display_height = 480,
                      .vertices = ui_vertices,
                      .indices = ui_indices,
                      .commands = {}},
                     resources)
            .ok());
  auto captured = capture(request);
  CHECK(captured && captured->window.width == 640 &&
        captured->views.front().scene.instances.size() == 1 &&
        captured->debug.lines().size() == 1 && captured->resources.get_mesh(1) == vertices.get() &&
        captured->resources.get_material(2) == description.get() &&
        !captured->resources.mesh_pending(1));
  request.debug.clear();
  request.ui.clear();
  CHECK(captured->ui.vertices().size() == 3 && captured->ui.indices().size() == 3 &&
        captured->ui.display_width() == 640);
  CHECK(pending->resources.get_material(2) == fallback && fallback->base_color_texture == 0);
  CHECK(pending->resources.mesh_pending(1) && waiting_textures->resources.texture_pending(3));
  for (std::size_t i = 0; i < textures.size(); ++i) {
    CHECK(captured->resources.get_texture(3 + i) == textures[i]->read().get());
    textures[i]->publish(std::make_shared<texture_data>());
  }
  mesh->publish(std::make_shared<mesh_data>());
  material->publish(std::make_shared<material_data>());
  scene.reset();
  mesh.reset();
  material.reset();
  textures = {};
  CHECK(captured->resources.get_mesh(1)->vertices.size() == 3 &&
        captured->resources.get_material(2)->emissive_texture == 7 &&
        captured->debug.lines().size() == 1);
  for (rid id = 3; id <= 7; ++id)
    CHECK(captured->resources.get_texture(id)->levels.front().pixels.front() == std::byte{1});
  return 0;
}
