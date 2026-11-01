// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/core/binary_reader.hpp"
#include "engine/core/binary_writer.hpp"
#include "engine/resource/mesh/mesh_data.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <string_view>

namespace gneiss::engine {
namespace {

constexpr std::array<std::byte, 4> magic = {std::byte{'G'}, std::byte{'N'}, std::byte{'M'},
                                            std::byte{'S'}};
constexpr std::uint16_t format_version = 3;
constexpr std::uint16_t vertex_stride = 72;
constexpr std::uint16_t header_size = 80;
constexpr std::uint16_t index_size = 4;
constexpr std::uint64_t vertex_offset = header_size;

void fail(mesh_binary_diagnostic& diagnostic, mesh_binary_error result, std::size_t offset,
          std::string_view message) noexcept {
  diagnostic.error = result;
  diagnostic.byte_offset = offset;
  try {
    diagnostic.message = message;
  } catch (...) {
    diagnostic.message.clear();
    diagnostic.error = mesh_binary_error::out_of_memory;
  }
}

[[nodiscard]] std::uint64_t align_16(std::uint64_t value) noexcept {
  return (value + 15U) & ~UINT64_C(15);
}

[[nodiscard]] bool finite(const mesh_binary_vertex& vertex) noexcept {
  return std::ranges::all_of(math::components(vertex.position),
                             [](float value) { return std::isfinite(value); }) &&
         std::ranges::all_of(math::components(vertex.texcoord),
                             [](float value) { return std::isfinite(value); }) &&
         std::ranges::all_of(math::components(vertex.normal),
                             [](float value) { return std::isfinite(value); });
}

[[nodiscard]] bool valid_normal(const math::vector3& normal) noexcept {
  const auto length =
      std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
  return std::abs(length - 1.0F) <= 1.0e-4F;
}

[[nodiscard]] bool checked_region(std::uint64_t offset, std::uint64_t count, std::uint64_t stride,
                                  std::uint64_t file_size) noexcept {
  return count <= (std::numeric_limits<std::uint64_t>::max() - offset) / stride &&
         offset + (count * stride) <= file_size;
}

} // namespace

bool is_mesh_binary(std::span<const std::byte> bytes) noexcept {
  return bytes.size() >= magic.size() && std::ranges::equal(bytes.first(magic.size()), magic);
}

result<std::vector<std::byte>, mesh_binary_diagnostic>
encode_mesh_binary(const mesh_binary_data& data) noexcept try {
  std::vector<std::byte> output;
  mesh_binary_diagnostic diagnostic;
  try {
    if (data.vertices.empty() || data.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        data.indices.size() < 3U || data.indices.size() % 3U != 0U ||
        data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail(diagnostic, mesh_binary_error::invalid_data, 0, "Mesh 数量超出范围或不是三角形列表");
      return diagnostic;
    }
    const std::uint32_t flags = (data.uv1.empty() ? 0U : 1U) | (data.colors.empty() ? 0U : 2U);
    if ((!data.uv1.empty() && data.uv1.size() != data.vertices.size()) ||
        (!data.colors.empty() && data.colors.size() != data.vertices.size()) ||
        !std::ranges::all_of(
            data.uv1,
            [](const auto& uv) { return std::isfinite(uv[0]) && std::isfinite(uv[1]); }) ||
        !std::ranges::all_of(data.colors, [](const auto& color) {
          return std::ranges::all_of(math::components(color), [](float v) {
            return std::isfinite(v) && v >= 0.0F && v <= 1.0F;
          });
        })) {
      fail(diagnostic, mesh_binary_error::invalid_data, 0, "UV1/顶点色数量或数值无效");
      return diagnostic;
    }
    if (data.tangents.size() != data.vertices.size()) {
      fail(diagnostic, mesh_binary_error::invalid_data, 0, "v3 必须提供与顶点数量一致的完整切线");
      return diagnostic;
    }
    for (std::size_t index = 0; index < data.tangents.size(); ++index) {
      if (!valid_mesh_tangent(data.tangents[index], data.vertices[index].normal)) {
        fail(diagnostic, mesh_binary_error::invalid_data, 0,
             "切线必须是与法线正交的单位向量，手性为正负一");
        return diagnostic;
      }
    }
    math::aabb3 bounds{data.vertices.front().position, data.vertices.front().position};
    for (const auto& vertex : data.vertices) {
      if (!finite(vertex) || !valid_normal(vertex.normal)) {
        fail(diagnostic, mesh_binary_error::invalid_data, 0, "Mesh 顶点包含无效数值或非单位法线");
        return diagnostic;
      }
      bounds = bounds.expanded(vertex.position);
    }
    for (const auto index : data.indices) {
      if (index >= data.vertices.size()) {
        fail(diagnostic, mesh_binary_error::invalid_data, 0, "Mesh 索引超出顶点范围");
        return diagnostic;
      }
    }
    const auto vertex_bytes = static_cast<std::uint64_t>(data.vertices.size()) * vertex_stride;
    const auto indices_offset = align_16(vertex_offset + vertex_bytes);
    const auto file_size =
        indices_offset + (static_cast<std::uint64_t>(data.indices.size()) * index_size);
    output.reserve(static_cast<std::size_t>(file_size));
    binary_writer writer{output};
    writer.write_bytes(magic);
    writer.write_le(format_version);
    writer.write_le(header_size);
    writer.write_le(flags);
    writer.write_le(static_cast<std::uint32_t>(data.vertices.size()));
    writer.write_le(static_cast<std::uint32_t>(data.indices.size()));
    writer.write_le(vertex_stride);
    writer.write_le(index_size);
    writer.write_le(vertex_offset);
    writer.write_le(indices_offset);
    writer.write_le(file_size);
    for (const auto value : math::components(bounds.min)) {
      writer.write_f32_le(value);
    }
    for (const auto value : math::components(bounds.max)) {
      writer.write_f32_le(value);
    }
    writer.write_le(UINT64_C(0));
    for (std::size_t index = 0; index < data.vertices.size(); ++index) {
      const auto& vertex = data.vertices[index];
      for (const auto value : math::components(vertex.position)) {
        writer.write_f32_le(value);
      }
      for (const auto value : math::components(vertex.texcoord)) {
        writer.write_f32_le(value);
      }
      for (const auto value : math::components(vertex.normal)) {
        writer.write_f32_le(value);
      }
      for (const auto value : math::components(data.tangents[index]))
        writer.write_f32_le(value);
      {
        const auto uv = data.uv1.empty() ? math::vector2{} : data.uv1[index];
        const auto color = data.colors.empty() ? math::vector4{1, 1, 1, 1} : data.colors[index];
        for (const auto value : math::components(uv))
          writer.write_f32_le(value);
        for (const auto value : math::components(color))
          writer.write_f32_le(value);
      }
    }
    writer.write_zeros(static_cast<std::size_t>(indices_offset) - writer.position());
    for (const auto index : data.indices) {
      writer.write_le(index);
    }
    return output;
  } catch (const std::bad_alloc&) {
    fail(diagnostic, mesh_binary_error::out_of_memory, 0, "内存不足");
    return diagnostic;
  } catch (...) {
    fail(diagnostic, mesh_binary_error::invalid_data, 0, "Mesh 编码内部错误");
    return diagnostic;
  }
} catch (...) {
  return mesh_binary_diagnostic{mesh_binary_error::out_of_memory, 0, {}};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 校验顺序与二进制布局一致。
result<mesh_binary_data, mesh_binary_diagnostic>
decode_mesh_binary(std::span<const std::byte> bytes) noexcept try {
  mesh_binary_data output;
  mesh_binary_diagnostic diagnostic;
  try {
    const binary_reader reader{bytes};
    // 各读取位置由下面的 Header、表及数据区边界检查保证有效。
    if (bytes.size() < header_size || !is_mesh_binary(bytes)) {
      fail(diagnostic, mesh_binary_error::invalid_data, 0, "Mesh Binary Header 无效或截断");
      return diagnostic;
    }
    const auto version = *reader.read_at_le<std::uint16_t>(4);
    if (version != format_version) {
      fail(diagnostic, mesh_binary_error::unsupported_version, 4, "不支持的 Mesh Binary 版本");
      return diagnostic;
    }
    const auto stored_header_size = *reader.read_at_le<std::uint16_t>(6);
    const auto flags = *reader.read_at_le<std::uint32_t>(8);
    const auto vertex_count = *reader.read_at_le<std::uint32_t>(12);
    const auto index_count = *reader.read_at_le<std::uint32_t>(16);
    const auto stored_vertex_stride = *reader.read_at_le<std::uint16_t>(20);
    const auto stored_index_size = *reader.read_at_le<std::uint16_t>(22);
    const auto stored_vertex_offset = *reader.read_at_le<std::uint64_t>(24);
    const auto stored_index_offset = *reader.read_at_le<std::uint64_t>(32);
    const auto stored_file_size = *reader.read_at_le<std::uint64_t>(40);
    const auto reserved = *reader.read_at_le<std::uint64_t>(72);
    if (stored_header_size != header_size || flags > 3U || stored_vertex_stride != vertex_stride ||
        stored_index_size != index_size || stored_vertex_offset != vertex_offset ||
        reserved != 0U || stored_file_size != bytes.size() || vertex_count == 0U ||
        index_count < 3U || index_count % 3U != 0U ||
        !checked_region(stored_vertex_offset, vertex_count, vertex_stride, stored_file_size) ||
        !checked_region(stored_index_offset, index_count, index_size, stored_file_size) ||
        stored_index_offset !=
            align_16(stored_vertex_offset +
                     (static_cast<std::uint64_t>(vertex_count) * vertex_stride))) {
      fail(diagnostic, mesh_binary_error::invalid_data, 6, "Mesh Binary 布局、数量或边界无效");
      return diagnostic;
    }
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      output.bounds.min[axis] = *reader.read_f32_at_le(48U + (axis * 4U));
      output.bounds.max[axis] = *reader.read_f32_at_le(60U + (axis * 4U));
      if (!std::isfinite(output.bounds.min[axis]) || !std::isfinite(output.bounds.max[axis]) ||
          output.bounds.min[axis] > output.bounds.max[axis]) {
        fail(diagnostic, mesh_binary_error::invalid_data, 48U + (axis * 4U),
             "Mesh Binary 包围盒无效");
        return diagnostic;
      }
    }
    output.vertices.resize(vertex_count);
    output.tangents.resize(vertex_count);
    if ((flags & 1U) != 0U)
      output.uv1.resize(vertex_count);
    if ((flags & 2U) != 0U)
      output.colors.resize(vertex_count);
    for (std::size_t index = 0; index < vertex_count; ++index) {
      const auto offset = static_cast<std::size_t>(stored_vertex_offset) + (index * vertex_stride);
      auto& vertex = output.vertices[index];
      for (std::size_t component = 0; component < 3U; ++component) {
        vertex.position[component] = *reader.read_f32_at_le(offset + (component * 4U));
        vertex.normal[component] = *reader.read_f32_at_le(offset + 20U + (component * 4U));
      }
      vertex.texcoord[0] = *reader.read_f32_at_le(offset + 12U);
      vertex.texcoord[1] = *reader.read_f32_at_le(offset + 16U);
      {
        auto& tangent = output.tangents[index];
        for (std::size_t component = 0; component < 4U; ++component) {
          tangent[component] = *reader.read_f32_at_le(offset + 32U + component * 4U);
        }
        if (!valid_mesh_tangent(tangent, vertex.normal)) {
          fail(diagnostic, mesh_binary_error::invalid_data, offset + 32U, "Mesh Binary 切线无效");
          return diagnostic;
        }
      }
      {
        math::vector2 uv{*reader.read_f32_at_le(offset + 48U),
                         *reader.read_f32_at_le(offset + 52U)};
        math::vector4 color{};
        for (std::size_t component = 0; component < 4U; ++component)
          color[component] = *reader.read_f32_at_le(offset + 56U + component * 4U);
        if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]) ||
            !std::ranges::all_of(math::components(color), [](float v) {
              return std::isfinite(v) && v >= 0.0F && v <= 1.0F;
            })) {
          fail(diagnostic, mesh_binary_error::invalid_data, offset + 48U, "UV1/顶点色无效");
          return diagnostic;
        }
        if (!output.uv1.empty())
          output.uv1[index] = uv;
        if (!output.colors.empty())
          output.colors[index] = color;
      }
      if (!finite(vertex) || !valid_normal(vertex.normal)) {
        fail(diagnostic, mesh_binary_error::invalid_data, offset,
             "Mesh Binary 顶点包含无效数值或非单位法线");
        return diagnostic;
      }
    }
    auto calculated_min = output.vertices.front().position;
    auto calculated_max = calculated_min;
    for (const auto& vertex : output.vertices) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        calculated_min[axis] = std::min(calculated_min[axis], vertex.position[axis]);
        calculated_max[axis] = std::max(calculated_max[axis], vertex.position[axis]);
      }
    }
    if (calculated_min != output.bounds.min || calculated_max != output.bounds.max) {
      fail(diagnostic, mesh_binary_error::invalid_data, 48, "Mesh Binary 包围盒与顶点不一致");
      return diagnostic;
    }
    output.indices.resize(index_count);
    for (std::size_t index = 0; index < index_count; ++index) {
      const auto offset = static_cast<std::size_t>(stored_index_offset) + (index * index_size);
      output.indices[index] = *reader.read_at_le<std::uint32_t>(offset);
      if (output.indices[index] >= vertex_count) {
        fail(diagnostic, mesh_binary_error::invalid_data, offset, "Mesh Binary 索引超出顶点范围");
        return diagnostic;
      }
    }
    return output;
  } catch (const std::bad_alloc&) {
    fail(diagnostic, mesh_binary_error::out_of_memory, 0, "内存不足");
    return diagnostic;
  } catch (...) {
    fail(diagnostic, mesh_binary_error::invalid_data, 0, "Mesh 解码内部错误");
    return diagnostic;
  }
} catch (...) {
  return mesh_binary_diagnostic{mesh_binary_error::out_of_memory, 0, {}};
}

std::string dump_mesh_binary_json(const mesh_binary_data& data) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<float>::max_digits10)
         << "{\n  \"format\": \"gneiss.mesh.debug\",\n  \"version\": " << format_version
         << ",\n  \"vertices\": [\n";
  for (std::size_t index = 0; index < data.vertices.size(); ++index) {
    const auto& value = data.vertices[index];
    stream << "    {\"position\":[" << value.position[0] << ',' << value.position[1] << ','
           << value.position[2] << "],\"uv\":[" << value.texcoord[0] << ',' << value.texcoord[1]
           << "],\"normal\":[" << value.normal[0] << ',' << value.normal[1] << ','
           << value.normal[2] << "]";
    if (!data.tangents.empty()) {
      const auto& tangent = data.tangents[index];
      stream << ",\"tangent\":[" << tangent[0] << ',' << tangent[1] << ',' << tangent[2] << ','
             << tangent[3] << ']';
    }
    if (!data.uv1.empty()) {
      stream << ",\"uv1\":[" << data.uv1[index][0] << ',' << data.uv1[index][1] << ']';
    }
    if (!data.colors.empty()) {
      const auto& color = data.colors[index];
      stream << ",\"color\":[" << color[0] << ',' << color[1] << ',' << color[2] << ',' << color[3]
             << ']';
    }
    stream << "}" << (index + 1U == data.vertices.size() ? "\n" : ",\n");
  }
  stream << "  ],\n  \"indices\": [";
  for (std::size_t index = 0; index < data.indices.size(); ++index) {
    stream << data.indices[index] << (index + 1U == data.indices.size() ? "" : ",");
  }
  stream << "]\n}\n";
  return stream.str();
}

} // namespace gneiss::engine
