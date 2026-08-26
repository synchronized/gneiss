// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/native_file_system.h"

#include "asset/asset_uri.h"

#include <fstream>
#include <iterator>
#include <new>
#include <system_error>

namespace {

[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text) {
  std::u8string value;
  value.reserve(text.size());
  for (const char character : text) {
    value.push_back(static_cast<char8_t>(character));
  }
  return {value};
}

} // namespace

namespace gneiss::asset_internal {

gneiss_result native_file_system::initialize(std::string_view root) noexcept {
  try {
    const auto path = path_from_utf8(root);
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    root_ = canonical;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result native_file_system::read(std::string_view path,
                                       std::vector<std::byte>& out_bytes) const noexcept {
  if (root_.empty()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
    std::string uri = "asset://";
    uri.append(path);
    if (validate_uri(uri) != GNEISS_SUCCESS) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }

    std::error_code error;
    const auto candidate = std::filesystem::canonical(root_ / path_from_utf8(path), error);
    if (error || !std::filesystem::is_regular_file(candidate, error) || error) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    const auto relative = std::filesystem::relative(candidate, root_, error);
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ifstream stream(candidate, std::ios::binary);
    if (!stream) {
      return GNEISS_ERROR_IO;
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
    if (stream.bad()) {
      return GNEISS_ERROR_IO;
    }
    std::vector<std::byte> bytes(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
      bytes[index] = static_cast<std::byte>(chars[index]);
    }
    out_bytes = std::move(bytes);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::asset_internal
