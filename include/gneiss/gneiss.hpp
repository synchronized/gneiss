// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GNEISS_HPP_
#define GNEISS_GNEISS_HPP_

#include <gneiss/engine/application.hpp>
#include <gneiss/engine/asset.hpp>
#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/core/rid.hpp>
#include <gneiss/engine/game_module.hpp>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>
#include <gneiss/engine/reflection.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/scene.hpp>
#include <gneiss/engine/world.hpp>
#include <gneiss/gneiss.h>

#include <cstdint>
#include <string_view>

namespace gneiss {

/** Gneiss 语义版本。 */
struct version {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;

  friend constexpr bool operator==(version, version) noexcept = default;
};

/** 编译当前程序时所用的 SDK 版本。 */
inline constexpr version header_version{GNEISS_VERSION_MAJOR, GNEISS_VERSION_MINOR,
                                        GNEISS_VERSION_PATCH};
inline constexpr std::string_view header_version_string = GNEISS_VERSION_STRING;

/** 返回运行时链接的 Gneiss 版本。 */
[[nodiscard]] inline version library_version() noexcept {
  return {gneiss_version_major(), gneiss_version_minor(), gneiss_version_patch()};
}

} // namespace gneiss

#endif
