// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_workspace.h"

#include "package_archive.h"

#include <yyjson.h>

#include <array>
#include <cctype>
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

[[nodiscard]] std::filesystem::path sdk_template_root() {
#if defined(_WIN32)
  char* sdk_root = nullptr;
  std::size_t length = 0U;
  if (_dupenv_s(&sdk_root, &length, "GNEISS_SDK_ROOT") != 0 || sdk_root == nullptr ||
      length <= 1U) {
    std::free(sdk_root);
    return {};
  }
  const std::filesystem::path root(sdk_root);
  std::free(sdk_root);
#else
  const auto* sdk_root = std::getenv("GNEISS_SDK_ROOT");
  if (sdk_root == nullptr || *sdk_root == '\0') {
    return {};
  }
  const std::filesystem::path root(sdk_root);
#endif
  return root / "share" / "gneiss" / "templates" / "game";
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] bool copy_directory(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::error_code& error) {
  std::filesystem::copy(source, destination,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::copy_symlinks,
                        error);
  return !error;
}

[[nodiscard]] std::string unique_suffix() {
  std::array<std::uint8_t, 8> bytes{};
  std::random_device random;
  for (auto& byte : bytes) {
    byte = static_cast<std::uint8_t>(random());
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    output << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return output.str();
}

[[nodiscard]] std::string module_id(std::string_view name) {
  std::string output{"gneiss.game."};
  bool separator = false;
  for (const auto character : name) {
    const auto value = static_cast<unsigned char>(character);
    if (std::isalnum(value) != 0) {
      output.push_back(static_cast<char>(std::tolower(value)));
      separator = false;
    } else if (!separator && output.back() != '.') {
      output.push_back('.');
      separator = true;
    }
  }
  while (output.back() == '.') {
    output.pop_back();
  }
  if (output == "gneiss.game") {
    output += ".project";
  }
  output += "." + unique_suffix();
  return output;
}

[[nodiscard]] bool replace_text(const std::filesystem::path& path, std::string_view from,
                                std::string_view to) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  std::size_t offset = 0U;
  bool replaced = false;
  while ((offset = text.find(from, offset)) != std::string::npos) {
    text.replace(offset, from.size(), to);
    offset += to.size();
    replaced = true;
  }
  return replaced && write_text(path, text);
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
#if defined(GNEISS_EDITOR_GAME_TEMPLATE_DIR)
  const auto installed_template = sdk_template_root();
  std::error_code error;
  if (!installed_template.empty() && std::filesystem::is_directory(installed_template, error) &&
      !error) {
    return create_editor_project(project_root, installed_template, name, output);
  }
  return create_editor_project(project_root, GNEISS_EDITOR_GAME_TEMPLATE_DIR, name, output);
#else
  (void)project_root;
  (void)name;
  (void)output;
  return result::not_found;
#endif
}

result create_editor_project(const std::filesystem::path& project_root,
                             const std::filesystem::path& template_root, std::string_view name,
                             editor_project& output) noexcept {
  if (project_root.empty() || name.empty()) {
    return result::invalid_argument;
  }
  try {
    std::error_code error;
    if (std::filesystem::exists(project_root, error) || error ||
        !std::filesystem::is_directory(template_root, error) || error ||
        !std::filesystem::is_regular_file(template_root / "gneiss.project.json", error) || error) {
      return result::invalid_state;
    }
    auto temporary = project_root;
    temporary += ".gneiss-creating";
    if (std::filesystem::exists(temporary, error) || error) {
      return result::invalid_state;
    }
    std::filesystem::create_directories(temporary.parent_path(), error);
    if (error) {
      return result::io;
    }
    if (!copy_directory(template_root, temporary, error)) {
      return result::io;
    }
    std::filesystem::create_directories(temporary / "sources", error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
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
        !yyjson_mut_obj_add_uint(document.get(), root, "version", 4U) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "name", name.data(), name.size()) ||
        !yyjson_mut_obj_add_str(document.get(), root, "asset_root", "assets") ||
        !yyjson_mut_obj_add_str(document.get(), root, "startup_scene",
                                "asset://scenes/main.scene.json")) {
      std::filesystem::remove_all(temporary, error);
      return result::out_of_memory;
    }
    auto* game_module = yyjson_mut_obj(document.get());
    auto* profiles = yyjson_mut_obj(document.get());
    auto* debug = yyjson_mut_obj(document.get());
    auto* development = yyjson_mut_obj(document.get());
    auto* shipping = yyjson_mut_obj(document.get());
    if (game_module == nullptr ||
        !yyjson_mut_obj_add_str(document.get(), game_module, "name", "gneiss_game") ||
        !yyjson_mut_obj_add_str(document.get(), game_module, "build_target", "gneiss_game") ||
        profiles == nullptr || debug == nullptr || development == nullptr || shipping == nullptr ||
        !yyjson_mut_obj_add_str(document.get(), debug, "directory", "modules/debug") ||
        !yyjson_mut_obj_add_str(document.get(), debug, "configure_preset",
                                "game-debug-configure") ||
        !yyjson_mut_obj_add_str(document.get(), debug, "build_preset", "game-debug") ||
        !yyjson_mut_obj_add_str(document.get(), development, "directory", "modules/development") ||
        !yyjson_mut_obj_add_str(document.get(), development, "configure_preset",
                                "game-development-configure") ||
        !yyjson_mut_obj_add_str(document.get(), development, "build_preset", "game-development") ||
        !yyjson_mut_obj_add_str(document.get(), shipping, "directory", "modules/shipping") ||
        !yyjson_mut_obj_add_str(document.get(), shipping, "configure_preset",
                                "game-shipping-configure") ||
        !yyjson_mut_obj_add_str(document.get(), shipping, "build_preset", "game-shipping") ||
        !yyjson_mut_obj_add_val(document.get(), profiles, "debug", debug) ||
        !yyjson_mut_obj_add_val(document.get(), profiles, "development", development) ||
        !yyjson_mut_obj_add_val(document.get(), profiles, "shipping", shipping) ||
        !yyjson_mut_obj_add_val(document.get(), game_module, "profiles", profiles) ||
        !yyjson_mut_obj_add_val(document.get(), root, "game_module", game_module)) {
      std::filesystem::remove_all(temporary, error);
      return result::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t length = 0;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
    if (!json ||
        !write_text(temporary / "gneiss.project.json", std::string_view(json.get(), length)) ||
        !replace_text(temporary / "game_module.cpp", "gneiss.template.game", module_id(name))) {
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

result export_editor_project(const editor_project& project,
                             const std::filesystem::path& runtime_executable,
                             const std::filesystem::path& output_root) noexcept {
  return export_editor_project(project, {.runtime_executable = runtime_executable,
                                         .output_root = output_root,
                                         .profile = app::game_build_profile::debug});
}

result export_editor_project(const editor_project& project,
                             const project_export_options& options) noexcept {
  if (project.project_root.empty() || options.runtime_executable.empty() ||
      options.output_root.empty()) {
    return result::invalid_argument;
  }
  try {
    const auto& build =
        app::game_build_profile_description_for(project.game_module, options.profile);
    if (build.directory.empty() || build.configure_preset.empty() || build.build_preset.empty()) {
      return result::unsupported;
    }
    std::error_code error;
    auto archive = options.output_root;
    archive += ".zip";
    if (std::filesystem::exists(options.output_root, error) || error ||
        (options.create_zip && std::filesystem::exists(archive, error)) || error ||
        !std::filesystem::is_regular_file(options.runtime_executable, error) || error) {
      return result::invalid_state;
    }
    auto temporary = options.output_root;
    temporary += ".gneiss-exporting";
    auto temporary_archive = archive;
    temporary_archive += ".gneiss-exporting";
    if (std::filesystem::exists(temporary, error) || error ||
        (options.create_zip && std::filesystem::exists(temporary_archive, error)) || error) {
      return result::invalid_state;
    }
    std::filesystem::create_directories(temporary.parent_path(), error);
    if (error) {
      return result::io;
    }
    std::filesystem::create_directories(temporary / "bin", error);
    std::filesystem::create_directories((temporary / build.directory).parent_path(), error);
    if (error || !copy_directory(project.asset_root, temporary / "assets", error) ||
        !copy_directory(project.project_root / build.directory, temporary / build.directory,
                        error)) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
    std::filesystem::copy_file(project.project_file, temporary / "gneiss.project.json",
                               std::filesystem::copy_options::none, error);
    if (!error) {
      std::filesystem::copy_file(options.runtime_executable,
                                 temporary / "bin" / options.runtime_executable.filename(),
                                 std::filesystem::copy_options::none, error);
    }
    if (error) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
    const std::array dependency_directories = {
        options.runtime_executable.parent_path(),
        options.runtime_executable.parent_path().parent_path() / "lib"};
    for (const auto& directory : dependency_directories) {
      if (!std::filesystem::is_directory(directory, error) || error) {
        error.clear();
        continue;
      }
      for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto filename = entry.path().filename().string();
        const auto extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".dll" || extension == ".dylib" ||
                                        filename.find(".so") != std::string::npos)) {
          std::filesystem::copy_file(entry.path(), temporary / "bin" / entry.path().filename(),
                                     std::filesystem::copy_options::none, error);
          if (error) {
            std::filesystem::remove_all(temporary, error);
            return result::io;
          }
        }
      }
    }
    const auto runtime_assets = options.runtime_executable.parent_path() / "assets";
    if (std::filesystem::is_directory(runtime_assets, error) && !error &&
        !copy_directory(runtime_assets, temporary / "bin" / "assets", error)) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
    const auto runtime_name = options.runtime_executable.filename().string();
    const auto profile_name = std::string(app::game_build_profile_name(options.profile));
    if (!write_text(temporary / "run.cmd", "@echo off\r\npushd \"%~dp0\"\r\n\"bin\\" +
                                               runtime_name + "\" --project . --profile " +
                                               profile_name +
                                               " %*\r\nset GNEISS_EXIT=%ERRORLEVEL%\r\npopd\r\n"
                                               "exit /b %GNEISS_EXIT%\r\n") ||
        !write_text(temporary / "run.sh",
                    "#!/bin/sh\nDIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
                    "LD_LIBRARY_PATH=\"$DIR/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\" "
                    "\"$DIR/bin/" +
                        runtime_name + "\" --project \"$DIR\" --profile " + profile_name +
                        " \"$@\"\n")) {
      std::filesystem::remove_all(temporary, error);
      return result::io;
    }
    std::filesystem::permissions(temporary / "run.sh",
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add, error);
    error.clear();
    const auto manifest_result = write_package_manifest(temporary, project, options.profile,
#if defined(_WIN32)
                                                        "run.cmd"
#else
                                                        "run.sh"
#endif
    );
    if (!manifest_result) {
      std::filesystem::remove_all(temporary, error);
      return manifest_result;
    }
    const auto verify_result = verify_package_manifest(temporary);
    if (!verify_result) {
      std::filesystem::remove_all(temporary, error);
      return verify_result;
    }
    if (options.create_zip) {
      const auto archive_result = write_deterministic_zip(temporary, temporary_archive);
      if (!archive_result) {
        std::filesystem::remove_all(temporary, error);
        std::filesystem::remove(temporary_archive, error);
        return archive_result;
      }
    }
    std::filesystem::rename(temporary, options.output_root, error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      std::filesystem::remove(temporary_archive, error);
      return result::io;
    }
    if (options.create_zip) {
      std::filesystem::rename(temporary_archive, archive, error);
      if (error) {
        std::filesystem::remove_all(options.output_root, error);
        std::filesystem::remove(temporary_archive, error);
        return result::io;
      }
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
