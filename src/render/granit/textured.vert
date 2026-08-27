// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#version 450

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_normal;

layout(set = 1, binding = 0) uniform ObjectUniform {
  mat4 model_view_projection;
  mat4 normal_matrix;
  vec4 color;
} object_uniform;

layout(location = 0) out vec4 vertex_color;
layout(location = 1) out vec2 vertex_uv;
layout(location = 2) out vec4 vertex_normal;

void main() {
  gl_Position = object_uniform.model_view_projection * in_position;
  vertex_color = object_uniform.color;
  vertex_uv = in_uv;
  vec3 transformed_normal = vec3(0.0);
  if (in_normal.w > 0.5) {
    transformed_normal =
        normalize((object_uniform.normal_matrix * vec4(in_normal.xyz, 0.0)).xyz);
  }
  vertex_normal = vec4(transformed_normal, in_normal.w);
}
