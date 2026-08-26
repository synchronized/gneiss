// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GNEISS_HPP_
#define GNEISS_GNEISS_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/gneiss.h>

#include <cstdint>

namespace gneiss {

/** Gneiss 语义版本。 */
struct version {
  std::uint32_t major;
  std::uint32_t minor;
  std::uint32_t patch;
};

/** 返回运行时链接的 Gneiss 版本。 */
[[nodiscard]] inline version library_version() noexcept {
  return {gneiss_version_major(), gneiss_version_minor(), gneiss_version_patch()};
}

} // namespace gneiss

#endif
