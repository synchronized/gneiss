// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_workspace.h"

#include <yyjson.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

namespace gneiss::editor {
namespace {

constexpr std::size_t maximum_recent_projects = 10U;

struct document_deleter final {
  void operator()(yyjson_doc* document) const noexcept { yyjson_doc_free(document); }
};

using document_ptr = std::unique_ptr<yyjson_doc, document_deleter>;

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] std::string make_uuid() {
  std::array<std::uint8_t, 16> bytes{};
  std::random_device random;
  for (auto& byte : bytes) {
    byte = static_cast<std::uint8_t>(random());
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4U || index == 6U || index == 8U || index == 10U) {
      output << '-';
    }
    output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return output.str();
}

[[nodiscard]] result write_recent_projects(const std::filesystem::path& state_file,
                                           const std::vector<editor_project>& projects) {
  std::error_code error;
  std::filesystem::create_directories(state_file.parent_path(), error);
  if (error) {
    return result::io;
  }
  yyjson_mut_doc* raw_document = yyjson_mut_doc_new(nullptr);
  if (raw_document == nullptr) {
    return result::out_of_memory;
  }
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr document(raw_document, &yyjson_mut_doc_free);
  auto* root = yyjson_mut_obj(document.get());
  auto* recent = yyjson_mut_arr(document.get());
  if (root == nullptr || recent == nullptr ||
      !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.editor-state") ||
      !yyjson_mut_obj_add_uint(document.get(), root, "version", 1U)) {
    return result::out_of_memory;
  }
  for (const auto& project : projects) {
    const auto path = path_utf8(project.project_root);
    if (!yyjson_mut_arr_add_strncpy(document.get(), recent, path.data(), path.size())) {
      return result::out_of_memory;
    }
  }
  if (!yyjson_mut_obj_add_val(document.get(), root, "recent_projects", recent)) {
    return result::out_of_memory;
  }
  yyjson_mut_doc_set_root(document.get(), root);
  std::size_t length = 0;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
  if (!json) {
    return result::out_of_memory;
  }
  const auto temporary = state_file.string() + ".tmp";
  if (!write_text(temporary, std::string_view(json.get(), length))) {
    return result::io;
  }
  std::filesystem::rename(temporary, state_file, error);
  if (error) {
    std::filesystem::remove(state_file, error);
    error.clear();
    std::filesystem::rename(temporary, state_file, error);
  }
  return error ? result::io : result::success;
}

} // namespace

std::filesystem::path default_editor_state_path() {
#if defined(_WIN32)
  char* local = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&local, &length, "LOCALAPPDATA") == 0 && local != nullptr && length > 1U) {
    const std::filesystem::path root(local);
    std::free(local);
    return root / "Gneiss" / "editor.json";
  }
  std::free(local);
#else
  if (const auto* config = std::getenv("XDG_CONFIG_HOME"); config != nullptr && *config != '\0') {
    return std::filesystem::path(config) / "gneiss" / "editor.json";
  }
  if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".config" / "gneiss" / "editor.json";
  }
#endif
  return std::filesystem::temp_directory_path() / "gneiss" / "editor.json";
}

result load_recent_projects(const std::filesystem::path& state_file,
                            std::vector<editor_project>& output) noexcept {
  try {
    output.clear();
    std::ifstream stream(state_file, std::ios::binary);
    if (!stream) {
      return result::success;
    }
    const std::string json{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    document_ptr document{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG)};
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* recent = yyjson_is_obj(root) ? yyjson_obj_get(root, "recent_projects") : nullptr;
    if (!yyjson_is_arr(recent)) {
      return result::invalid_argument;
    }
    std::size_t index = 0;
    std::size_t maximum = 0;
    yyjson_val* value = nullptr;
    yyjson_arr_foreach(recent, index, maximum, value) {
      if (!yyjson_is_str(value) || output.size() >= maximum_recent_projects) {
        continue;
      }
      editor_project project;
      const std::string_view path(yyjson_get_str(value), yyjson_get_len(value));
      if (load_editor_project(utf8_path(path), project) == result::success) {
        output.push_back(std::move(project));
      }
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result remember_recent_project(const std::filesystem::path& state_file,
                               const editor_project& project) noexcept {
  try {
    std::vector<editor_project> projects;
    const auto load_result = load_recent_projects(state_file, projects);
    if (load_result != result::success && load_result != result::invalid_argument) {
      return load_result;
    }
    std::erase_if(projects, [&](const auto& candidate) {
      return candidate.project_root == project.project_root;
    });
    projects.insert(projects.begin(), project);
    if (projects.size() > maximum_recent_projects) {
      projects.resize(maximum_recent_projects);
    }
    return write_recent_projects(state_file, projects);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result create_editor_project(const std::filesystem::path& project_root, std::string_view name,
                             editor_project& output) noexcept {
  if (project_root.empty() || name.empty()) {
    return result::invalid_argument;
  }
  try {
    std::error_code error;
    if (std::filesystem::exists(project_root, error) || error) {
      return result::invalid_state;
    }
    auto temporary = project_root;
    temporary += ".gneiss-creating";
    if (std::filesystem::exists(temporary, error) || error) {
      return result::invalid_state;
    }
    std::filesystem::create_directories(temporary / "assets" / "scenes", error);
    if (!error) {
      std::filesystem::create_directories(temporary / "sources", error);
    }
    if (error) {
      return result::io;
    }
    const auto scene_uuid = make_uuid();
    const auto node_uuid = make_uuid();
    const std::string scene = "{\n  \"format\": \"gneiss.scene\",\n  \"version\": 2,\n  "
                              "\"scene_uuid\": \"" +
                              scene_uuid + "\",\n  \"objects\": [\n    {\n      \"uuid\": \"" +
                              node_uuid +
                              "\",\n      \"name\": \"Camera\",\n      \"parent\": null,\n      "
                              "\"transform\": {\"translation\": [0, 0, 3], "
                              "\"rotation\": [0, 0, 0, 1], \"scale\": [1, 1, 1]},\n      "
                              "\"components\": {\"camera\": {"
                              "\"vertical_field_of_view_radians\": 1.04719755, "
                              "\"near_plane\": 0.1, \"far_plane\": 1000, "
                              "\"is_primary\": true}}\n    }\n  ]\n}\n";
    yyjson_mut_doc* raw_document = yyjson_mut_doc_new(nullptr);
    if (raw_document == nullptr) {
      std::filesystem::remove_all(temporary, error);
      return result::out_of_memory;
    }
    using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    mutable_document_ptr document(raw_document, &yyjson_mut_doc_free);
    auto* root = yyjson_mut_obj(document.get());
    if (root == nullptr ||
        !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.project") ||
        !yyjson_mut_obj_add_uint(document.get(), root, "version", 1U) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "name", name.data(), name.size()) ||
        !yyjson_mut_obj_add_str(document.get(), root, "asset_root", "assets") ||
        !yyjson_mut_obj_add_str(document.get(), root, "startup_scene",
                                "asset://scenes/main.scene.json")) {
      std::filesystem::remove_all(temporary, error);
      return result::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t length = 0;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
    if (!json ||
        !write_text(temporary / "gneiss.project.json", std::string_view(json.get(), length)) ||
        !write_text(temporary / "assets" / "scenes" / "main.scene.json", scene)) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
    std::filesystem::rename(temporary, project_root, error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
    return load_editor_project(project_root, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
