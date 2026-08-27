#version 450

layout(set = 0, binding = 0) uniform texture2D base_color_texture;
layout(set = 0, binding = 1) uniform sampler base_color_sampler;

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec2 vertex_uv;
layout(location = 2) in vec4 vertex_normal;
layout(location = 0) out vec4 out_color;

void main() {
  vec4 base_color =
      texture(sampler2D(base_color_texture, base_color_sampler), vertex_uv) * vertex_color;
  float light = 1.0;
  if (vertex_normal.w > 0.5) {
    vec3 direction_to_light = normalize(vec3(0.4, 0.8, 0.6));
    float diffuse = max(dot(normalize(vertex_normal.xyz), direction_to_light), 0.0);
    light = 0.2 + (0.8 * diffuse);
  }
  out_color = vec4(base_color.rgb * light, base_color.a);
}
