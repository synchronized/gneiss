// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "material_description.hpp"
#include <cmath>
namespace gneiss::engine {
namespace {
serialization::diagnostic invalid_description(std::string path, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
}
serialization::diagnostic validate_material_parameters(const material_parameters& v) {
  const auto unit = [](float n) { return std::isfinite(n) && n >= 0 && n <= 1; };
  for (std::size_t i = 0; i < 4; ++i)
    if (!unit(v.color[i]))
      return invalid_description("/color/" + std::to_string(i));
  if (!unit(v.metallic))
    return invalid_description("/metallic");
  if (!unit(v.roughness))
    return invalid_description("/roughness");
  if (!unit(v.occlusion_strength))
    return invalid_description("/occlusion_strength");
  if (!std::isfinite(v.normal_scale))
    return invalid_description("/normal_scale");
  for (float n : math::components(v.emissive))
    if (!unit(n))
      return invalid_description("/emissive");
  if (!std::isfinite(v.alpha_cutoff) || v.alpha_cutoff < 0)
    return invalid_description("/alpha_cutoff");
  if (v.alpha_mode > material_alpha_mode::blend)
    return invalid_description("/alpha_mode");
  for (std::size_t i = 0; i < v.sampling.size(); ++i)
    if (!valid_sampling(v.sampling[i]))
      return invalid_description("/sampling/" + std::to_string(i), "采样值越界");
  return {};
}
serialization::diagnostic validate_material_description(const material_description& v) {
  auto diagnostic = validate_material_parameters(v.parameters);
  if (diagnostic.error.failed())
    diagnostic.path.insert(0, "/parameters");
  return diagnostic;
}
}
