// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/ktx2_probe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
  for (std::size_t index = 0U; index < 8U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

} // namespace

int main() {
  constexpr std::array<std::uint8_t, 12U> identifier = {0xABU, 0x4BU, 0x54U, 0x58U, 0x20U, 0x32U,
                                                        0x30U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  std::vector<std::uint8_t> bytes(148U);
  std::copy(identifier.begin(), identifier.end(), bytes.begin());
  write_u32(bytes, 12U, 43U);
  write_u32(bytes, 16U, 1U);
  write_u32(bytes, 20U, 2U);
  write_u32(bytes, 24U, 2U);
  write_u32(bytes, 36U, 1U);
  write_u32(bytes, 40U, 2U);
  write_u64(bytes, 80U, 128U);
  write_u64(bytes, 88U, 16U);
  write_u64(bytes, 96U, 16U);
  write_u64(bytes, 104U, 144U);
  write_u64(bytes, 112U, 4U);
  write_u64(bytes, 120U, 4U);

  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-ktx2-probe-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "representative.ktx2";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();

  const auto report = gneiss::tooling::asset_build::inspect_ktx2(path);
  if (report.result != gneiss::tooling::asset_build::ktx2_probe_result::success ||
      report.information.width != 2U || report.information.height != 2U ||
      report.information.level_count != 2U ||
      report.information.transfer_function !=
          gneiss::tooling::asset_build::ktx2_transfer_function::srgb ||
      !report.information.has_alpha ||
      report.information.supercompression !=
          gneiss::tooling::asset_build::ktx2_supercompression::none) {
    return 1;
  }
  bytes[0] = 0U;
  std::ofstream invalid(path, std::ios::binary | std::ios::trunc);
  invalid.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
  invalid.close();
  if (gneiss::tooling::asset_build::inspect_ktx2(path).result !=
      gneiss::tooling::asset_build::ktx2_probe_result::invalid_container) {
    return 2;
  }
  std::error_code error;
  std::filesystem::remove_all(root, error);
  return 0;
}
