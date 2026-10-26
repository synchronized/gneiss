// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/asset_uri.hpp"

#include <string_view>

using gneiss::engine::result;
using gneiss::engine::validate_uri;

int main() {
  static const unsigned char invalid_utf8[] = {'a', 's', 's',  'e',  't', ':',
                                               '/', '/', 0xC0, 0xAF, 0};
  if (validate_uri("asset://models/triangle.mesh") != result::success ||
      validate_uri("asset://纹理/石头.png") != result::success ||
      validate_uri("file://models/triangle.mesh") != result::invalid_argument ||
      validate_uri("asset://") != result::invalid_argument ||
      validate_uri("asset:///root") != result::invalid_argument ||
      validate_uri("asset://models//triangle.mesh") != result::invalid_argument ||
      validate_uri("asset://models/./triangle.mesh") != result::invalid_argument ||
      validate_uri("asset://models/../secret") != result::invalid_argument ||
      validate_uri("asset://C:/secret") != result::invalid_argument ||
      validate_uri("asset://models\\triangle.mesh") != result::invalid_argument ||
      validate_uri("asset://models/a%2Fb") != result::invalid_argument ||
      validate_uri(reinterpret_cast<const char*>(invalid_utf8)) != result::invalid_argument ||
      validate_uri({}) != result::invalid_argument) {
    return 1;
  }

  return 0;
}
