// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/tangent_generation.hpp"

#include "engine/asset/mesh_tangent.hpp"

#include <mikktspace.h>

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace gneiss::tooling::asset_import {
namespace {

struct tangent_context {
  const import_ir_primitive& primitive;
  std::vector<std::array<float, 4>> corners;
};

tangent_context& context(const SMikkTSpaceContext* source) {
  return *static_cast<tangent_context*>(source->m_pUserData);
}

const import_ir_primitive::vertex& vertex(const SMikkTSpaceContext* source, int face, int corner) {
  const auto& primitive = context(source).primitive;
  const auto index = (static_cast<std::size_t>(face) * 3U) + static_cast<std::size_t>(corner);
  return primitive.vertices[primitive.indices[index]];
}

} // namespace

std::optional<std::string> generate_tangents(import_ir_primitive& primitive) {
  if (primitive.indices.empty() || primitive.indices.size() % 3U != 0U ||
      primitive.indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      !std::ranges::all_of(primitive.indices,
                           [&](auto index) { return index < primitive.vertices.size(); })) {
    return "切线生成需要有效的索引三角形列表";
  }
  tangent_context data{.primitive = primitive,
                       .corners = std::vector<std::array<float, 4>>(primitive.indices.size())};
  SMikkTSpaceInterface callbacks{};
  callbacks.m_getNumFaces = [](const SMikkTSpaceContext* source) {
    return static_cast<int>(context(source).primitive.indices.size() / 3U);
  };
  callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
  callbacks.m_getPosition = [](const SMikkTSpaceContext* source, float output[], int face,
                               int corner) {
    std::ranges::copy(vertex(source, face, corner).position, output);
  };
  callbacks.m_getNormal = [](const SMikkTSpaceContext* source, float output[], int face,
                             int corner) {
    std::ranges::copy(vertex(source, face, corner).normal, output);
  };
  callbacks.m_getTexCoord = [](const SMikkTSpaceContext* source, float output[], int face,
                               int corner) {
    const auto& value = vertex(source, face, corner);
    if (context(source).primitive.tangent_uv_set == 1U)
      std::ranges::copy(value.uv1, output);
    else
      std::ranges::copy(value.texcoord, output);
  };
  callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* source, const float tangent[],
                                  float sign, int face, int corner) {
    const auto index = (static_cast<std::size_t>(face) * 3U) + static_cast<std::size_t>(corner);
    context(source).corners[index] = {tangent[0], tangent[1], tangent[2], sign};
  };
  SMikkTSpaceContext source{.m_pInterface = &callbacks, .m_pUserData = &data};
  if (genTangSpaceDefault(&source) == 0) {
    return "MikkTSpace 无法生成切线";
  }
  std::vector<import_ir_primitive::vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::map<std::pair<std::uint32_t, std::array<float, 4>>, std::uint32_t> mapping;
  vertices.reserve(primitive.vertices.size());
  indices.reserve(primitive.indices.size());
  for (std::size_t index = 0; index < primitive.indices.size(); ++index) {
    const auto original = primitive.indices[index];
    const auto& normal = primitive.vertices[original].normal;
    const auto& tangent = data.corners[index];
    if (!asset_internal::valid_mesh_tangent(tangent, {normal[0], normal[1], normal[2]})) {
      return "退化几何或 UV 无法生成有效的正交切线";
    }
    const auto key = std::pair{original, tangent};
    const auto [found, inserted] =
        mapping.emplace(key, static_cast<std::uint32_t>(vertices.size()));
    if (inserted) {
      auto value = primitive.vertices[original];
      value.tangent = tangent;
      vertices.push_back(value);
    }
    indices.push_back(found->second);
  }
  primitive.vertices = std::move(vertices);
  primitive.indices = std::move(indices);
  return std::nullopt;
}

} // namespace gneiss::tooling::asset_import
