// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/tangent_generation.h"

#include <cmath>
#include <limits>

int main() {
  using namespace gneiss::tooling::asset_import;
  import_ir_primitive mesh;
  mesh.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                   {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                   {{0, 1, 0}, {0, 0, 1}, {0, 1}},
                   {{-1, 0, 0}, {0, 0, 1}, {1, 0}},
                   {{0, -1, 0}, {0, 0, 1}, {0, -1}}};
  mesh.indices = {0, 1, 2, 0, 3, 4};
  if (generate_tangents(mesh) || mesh.vertices.size() != 6U || mesh.indices.size() != 6U ||
      mesh.indices[0] == mesh.indices[3]) {
    return 1;
  }
  const auto& first = mesh.vertices[mesh.indices[0]].tangent;
  const auto& mirrored = mesh.vertices[mesh.indices[3]].tangent;
  if (std::abs(first[0] - 1.0F) > 1.0e-5F || first[3] != 1.0F ||
      std::abs(mirrored[0] + 1.0F) > 1.0e-5F || mirrored[3] != -1.0F) {
    return 2;
  }
  const auto before = mesh;
  if (generate_tangents(mesh) || mesh.indices != before.indices ||
      mesh.vertices.size() != before.vertices.size()) {
    return 3;
  }
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    if (mesh.vertices[index].tangent != before.vertices[index].tangent)
      return 4;
  }
  mesh.indices[0] = std::numeric_limits<std::uint32_t>::max();
  const auto invalid = mesh.indices;
  if (!generate_tangents(mesh) || mesh.indices != invalid)
    return 5;
  mesh.indices = before.indices;
  mesh.vertices[0].normal[2] = 0.0F;
  if (!generate_tangents(mesh) || mesh.indices != before.indices)
    return 6;
  return 0;
}
