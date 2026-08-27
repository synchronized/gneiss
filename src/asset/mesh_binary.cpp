// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/mesh_binary.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <string_view>

namespace gneiss::asset_internal {
namespace {

constexpr std::array<std::byte, 4> magic = {std::byte{'G'}, std::byte{'N'}, std::byte{'M'},
                                            std::byte{'S'}};
constexpr std::uint16_t format_version = 1;
constexpr std::uint16_t header_size = 80;
constexpr std::uint16_t vertex_stride = 32;
constexpr std::uint16_t index_size = 4;
constexpr std::uint64_t vertex_offset = header_size;

void fail(mesh_binary_diagnostic& diagnostic, mesh_binary_result result, std::size_t offset,
          std::string_view message) noexcept {
  diagnostic.result = result;
  diagnostic.byte_offset = offset;
  try {
    diagnostic.message = message;
  } catch (...) {
    diagnostic.message.clear();
  }
}

[[nodiscard]] std::uint64_t align_16(std::uint64_t value) noexcept {
  return (value + 15U) & ~UINT64_C(15);
}

template <typename Integer> void append_integer(std::vector<std::byte>& output, Integer value) {
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    output.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

void append_float(std::vector<std::byte>& output, float value) {
  append_integer(output, std::bit_cast<std::uint32_t>(value));
}

template <typename Integer>
[[nodiscard]] Integer read_integer(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  Integer value{};
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    value |= static_cast<Integer>(std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] float read_float(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  return std::bit_cast<float>(read_integer<std::uint32_t>(bytes, offset));
}

[[nodiscard]] bool finite(const mesh_binary_vertex& vertex) noexcept {
  return std::ranges::all_of(vertex.position, [](float value) { return std::isfinite(value); }) &&
         std::ranges::all_of(vertex.texcoord, [](float value) { return std::isfinite(value); }) &&
         std::ranges::all_of(vertex.normal, [](float value) { return std::isfinite(value); });
}

[[nodiscard]] bool valid_normal(const std::array<float, 3>& normal) noexcept {
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

mesh_binary_result encode_mesh_binary(const mesh_binary_data& data, std::vector<std::byte>& output,
                                      mesh_binary_diagnostic& diagnostic) noexcept {
  output.clear();
  diagnostic = {};
  try {
    if (data.vertices.empty() || data.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        data.indices.size() < 3U || data.indices.size() % 3U != 0U ||
        data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail(diagnostic, mesh_binary_result::invalid_data, 0, "Mesh 数量超出范围或不是三角形列表");
      return diagnostic.result;
    }
    auto bounds_min = data.vertices.front().position;
    auto bounds_max = bounds_min;
    for (const auto& vertex : data.vertices) {
      if (!finite(vertex) || !valid_normal(vertex.normal)) {
        fail(diagnostic, mesh_binary_result::invalid_data, 0, "Mesh 顶点包含无效数值或非单位法线");
        return diagnostic.result;
      }
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        bounds_min[axis] = std::min(bounds_min[axis], vertex.position[axis]);
        bounds_max[axis] = std::max(bounds_max[axis], vertex.position[axis]);
      }
    }
    for (const auto index : data.indices) {
      if (index >= data.vertices.size()) {
        fail(diagnostic, mesh_binary_result::invalid_data, 0, "Mesh 索引超出顶点范围");
        return diagnostic.result;
      }
    }
    const auto vertex_bytes = static_cast<std::uint64_t>(data.vertices.size()) * vertex_stride;
    const auto indices_offset = align_16(vertex_offset + vertex_bytes);
    const auto file_size =
        indices_offset + (static_cast<std::uint64_t>(data.indices.size()) * index_size);
    output.reserve(static_cast<std::size_t>(file_size));
    output.insert(output.end(), magic.begin(), magic.end());
    append_integer(output, format_version);
    append_integer(output, header_size);
    append_integer(output, UINT32_C(0));
    append_integer(output, static_cast<std::uint32_t>(data.vertices.size()));
    append_integer(output, static_cast<std::uint32_t>(data.indices.size()));
    append_integer(output, vertex_stride);
    append_integer(output, index_size);
    append_integer(output, vertex_offset);
    append_integer(output, indices_offset);
    append_integer(output, file_size);
    for (const auto value : bounds_min) {
      append_float(output, value);
    }
    for (const auto value : bounds_max) {
      append_float(output, value);
    }
    append_integer(output, UINT64_C(0));
    for (const auto& vertex : data.vertices) {
      for (const auto value : vertex.position) {
        append_float(output, value);
      }
      for (const auto value : vertex.texcoord) {
        append_float(output, value);
      }
      for (const auto value : vertex.normal) {
        append_float(output, value);
      }
    }
    output.resize(static_cast<std::size_t>(indices_offset), std::byte{});
    for (const auto index : data.indices) {
      append_integer(output, index);
    }
    return mesh_binary_result::success;
  } catch (const std::bad_alloc&) {
    output.clear();
    fail(diagnostic, mesh_binary_result::invalid_data, 0, "内存不足");
    return diagnostic.result;
  } catch (...) {
    output.clear();
    fail(diagnostic, mesh_binary_result::invalid_data, 0, "Mesh 编码内部错误");
    return diagnostic.result;
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 校验顺序与二进制布局一致。
mesh_binary_result decode_mesh_binary(std::span<const std::byte> bytes, mesh_binary_data& output,
                                      mesh_binary_diagnostic& diagnostic) noexcept {
  output = {};
  diagnostic = {};
  try {
    if (bytes.size() < header_size || !is_mesh_binary(bytes)) {
      fail(diagnostic, mesh_binary_result::invalid_data, 0, "Mesh Binary Header 无效或截断");
      return diagnostic.result;
    }
    const auto version = read_integer<std::uint16_t>(bytes, 4);
    if (version != format_version) {
      fail(diagnostic, mesh_binary_result::unsupported_version, 4, "不支持的 Mesh Binary 版本");
      return diagnostic.result;
    }
    const auto stored_header_size = read_integer<std::uint16_t>(bytes, 6);
    const auto flags = read_integer<std::uint32_t>(bytes, 8);
    const auto vertex_count = read_integer<std::uint32_t>(bytes, 12);
    const auto index_count = read_integer<std::uint32_t>(bytes, 16);
    const auto stored_vertex_stride = read_integer<std::uint16_t>(bytes, 20);
    const auto stored_index_size = read_integer<std::uint16_t>(bytes, 22);
    const auto stored_vertex_offset = read_integer<std::uint64_t>(bytes, 24);
    const auto stored_index_offset = read_integer<std::uint64_t>(bytes, 32);
    const auto stored_file_size = read_integer<std::uint64_t>(bytes, 40);
    const auto reserved = read_integer<std::uint64_t>(bytes, 72);
    if (stored_header_size != header_size || flags != 0U || stored_vertex_stride != vertex_stride ||
        stored_index_size != index_size || stored_vertex_offset != vertex_offset ||
        reserved != 0U || stored_file_size != bytes.size() || vertex_count == 0U ||
        index_count < 3U || index_count % 3U != 0U ||
        !checked_region(stored_vertex_offset, vertex_count, vertex_stride, stored_file_size) ||
        !checked_region(stored_index_offset, index_count, index_size, stored_file_size) ||
        stored_index_offset !=
            align_16(stored_vertex_offset +
                     (static_cast<std::uint64_t>(vertex_count) * vertex_stride))) {
      fail(diagnostic, mesh_binary_result::invalid_data, 6, "Mesh Binary 布局、数量或边界无效");
      return diagnostic.result;
    }
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      output.bounds_min[axis] = read_float(bytes, 48U + (axis * 4U));
      output.bounds_max[axis] = read_float(bytes, 60U + (axis * 4U));
      if (!std::isfinite(output.bounds_min[axis]) || !std::isfinite(output.bounds_max[axis]) ||
          output.bounds_min[axis] > output.bounds_max[axis]) {
        fail(diagnostic, mesh_binary_result::invalid_data, 48U + (axis * 4U),
             "Mesh Binary 包围盒无效");
        return diagnostic.result;
      }
    }
    output.vertices.resize(vertex_count);
    for (std::size_t index = 0; index < vertex_count; ++index) {
      const auto offset = static_cast<std::size_t>(stored_vertex_offset) + (index * vertex_stride);
      auto& vertex = output.vertices[index];
      for (std::size_t component = 0; component < 3U; ++component) {
        vertex.position[component] = read_float(bytes, offset + (component * 4U));
        vertex.normal[component] = read_float(bytes, offset + 20U + (component * 4U));
      }
      vertex.texcoord[0] = read_float(bytes, offset + 12U);
      vertex.texcoord[1] = read_float(bytes, offset + 16U);
      if (!finite(vertex) || !valid_normal(vertex.normal)) {
        fail(diagnostic, mesh_binary_result::invalid_data, offset,
             "Mesh Binary 顶点包含无效数值或非单位法线");
        return diagnostic.result;
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
    if (calculated_min != output.bounds_min || calculated_max != output.bounds_max) {
      fail(diagnostic, mesh_binary_result::invalid_data, 48, "Mesh Binary 包围盒与顶点不一致");
      return diagnostic.result;
    }
    output.indices.resize(index_count);
    for (std::size_t index = 0; index < index_count; ++index) {
      const auto offset = static_cast<std::size_t>(stored_index_offset) + (index * index_size);
      output.indices[index] = read_integer<std::uint32_t>(bytes, offset);
      if (output.indices[index] >= vertex_count) {
        fail(diagnostic, mesh_binary_result::invalid_data, offset, "Mesh Binary 索引超出顶点范围");
        return diagnostic.result;
      }
    }
    return mesh_binary_result::success;
  } catch (const std::bad_alloc&) {
    output = {};
    fail(diagnostic, mesh_binary_result::invalid_data, 0, "内存不足");
    return diagnostic.result;
  } catch (...) {
    output = {};
    fail(diagnostic, mesh_binary_result::invalid_data, 0, "Mesh 解码内部错误");
    return diagnostic.result;
  }
}

std::string dump_mesh_binary_json(const mesh_binary_data& data) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<float>::max_digits10)
         << "{\n  \"format\": \"gneiss.mesh.debug\",\n  \"version\": 1,\n  \"vertices\": [\n";
  for (std::size_t index = 0; index < data.vertices.size(); ++index) {
    const auto& value = data.vertices[index];
    stream << "    {\"position\":[" << value.position[0] << ',' << value.position[1] << ','
           << value.position[2] << "],\"uv\":[" << value.texcoord[0] << ',' << value.texcoord[1]
           << "],\"normal\":[" << value.normal[0] << ',' << value.normal[1] << ','
           << value.normal[2] << "]}" << (index + 1U == data.vertices.size() ? "\n" : ",\n");
  }
  stream << "  ],\n  \"indices\": [";
  for (std::size_t index = 0; index < data.indices.size(); ++index) {
    stream << data.indices[index] << (index + 1U == data.indices.size() ? "" : ",");
  }
  stream << "]\n}\n";
  return stream.str();
}

} // namespace gneiss::asset_internal
