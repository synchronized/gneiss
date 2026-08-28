// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_project.h"

#include <gneiss/asset.h>

#include <yyjson.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <string_view>
#include <system_error>

namespace gneiss::editor {
namespace {

constexpr std::string_view project_filename = "gneiss.project.json";

struct document_deleter final {
  void operator()(yyjson_doc* document) const noexcept { yyjson_doc_free(document); }
};

using document_ptr = std::unique_ptr<yyjson_doc, document_deleter>;

[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& path) noexcept {
  auto root_part = root.begin();
  auto path_part = path.begin();
  for (; root_part != root.end(); ++root_part, ++path_part) {
    if (path_part == path.end() || *root_part != *path_part) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool read_string(yyjson_val* object, const char* key, std::string& output) {
  auto* value = yyjson_obj_get(object, key);
  if (!yyjson_is_str(value)) {
    return false;
  }
  output.assign(yyjson_get_str(value), yyjson_get_len(value));
  return !output.empty();
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view value) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

[[nodiscard]] bool valid_relative_directory(std::string_view value) noexcept {
  if (value.empty() || value.front() == '/' || value.back() == '/') {
    return false;
  }
  std::size_t segment_start = 0;
  while (segment_start < value.size()) {
    const auto separator = value.find('/', segment_start);
    const auto segment = value.substr(segment_start, separator - segment_start);
    if (segment.empty() || segment == "." || segment == "..") {
      return false;
    }
    for (const auto character : segment) {
      const auto byte = static_cast<unsigned char>(character);
      if (byte < 0x20U || character == '\\' || character == ':' || character == '%' ||
          character == '?' || character == '#') {
        return false;
      }
    }
    if (separator == std::string_view::npos) {
      break;
    }
    segment_start = separator + 1U;
  }
  return true;
}

} // namespace

result load_editor_project(const std::filesystem::path& input, editor_project& output) noexcept {
  if (input.empty()) {
    return result::invalid_argument;
  }
  try {
    std::error_code error;
    auto project_file = input;
    if (std::filesystem::is_directory(project_file, error) && !error) {
      project_file /= project_filename;
    }
    if (error || !std::filesystem::is_regular_file(project_file, error) || error) {
      return result::not_found;
    }
    project_file = std::filesystem::canonical(project_file, error);
    if (error || project_file.filename() != project_filename) {
      return result::invalid_argument;
    }

    std::ifstream stream(project_file, std::ios::binary);
    if (!stream) {
      return result::io;
    }
    const std::string json{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    if (stream.bad() || json.empty()) {
      return result::io;
    }
    document_ptr document{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG)};
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(root)) {
      return result::invalid_argument;
    }
    auto* format = yyjson_obj_get(root, "format");
    auto* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_str(format) ||
        std::string_view(yyjson_get_str(format), yyjson_get_len(format)) != "gneiss.project" ||
        !yyjson_is_uint(version)) {
      return result::invalid_argument;
    }
    if (yyjson_get_uint(version) != 1U) {
      return result::unsupported;
    }
    editor_project pending;
    std::string asset_root_text;
    if (!read_string(root, "name", pending.name) ||
        !read_string(root, "asset_root", asset_root_text) ||
        !read_string(root, "startup_scene", pending.startup_scene) ||
        !valid_relative_directory(asset_root_text) ||
        gneiss_asset_uri_validate(pending.startup_scene.data(), pending.startup_scene.size()) !=
            GNEISS_SUCCESS) {
      return result::invalid_argument;
    }

    pending.project_file = project_file;
    pending.project_root = project_file.parent_path();
    pending.asset_root =
        std::filesystem::weakly_canonical(pending.project_root / utf8_path(asset_root_text), error);
    if (error || !is_within(pending.project_root, pending.asset_root) ||
        !std::filesystem::is_directory(pending.asset_root, error) || error) {
      return result::not_found;
    }
    constexpr std::string_view scheme = "asset://";
    const auto scene_path = std::filesystem::weakly_canonical(
        pending.asset_root /
            utf8_path(std::string_view(pending.startup_scene).substr(scheme.size())),
        error);
    if (error || !is_within(pending.asset_root, scene_path) ||
        !std::filesystem::is_regular_file(scene_path, error) || error) {
      return result::not_found;
    }
    output = std::move(pending);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
