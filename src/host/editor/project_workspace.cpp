// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_workspace.hpp"
#include "engine/core/error.hpp"

#include "package_archive.hpp"

#include "tooling/asset_build/asset_build.hpp"

#include <yyjson.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

namespace gneiss::host {
namespace {

constexpr std::size_t maximum_recent_projects = 10U;

[[nodiscard]] constexpr std::string_view platform_name() noexcept {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

[[nodiscard]] constexpr std::string_view architecture_name() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
  return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#else
  return "unknown";
#endif
}

[[nodiscard]] gneiss::engine::error
map_asset_build_result(gneiss::tooling::build_result operation) {
  using gneiss::tooling::build_result;
  switch (operation) {
  case build_result::success:
    return gneiss::engine::error::success;
  case build_result::invalid_argument:
    return gneiss::engine::error::invalid_argument;
  case build_result::source_unavailable:
  case build_result::dependency_missing:
    return gneiss::engine::error::not_found;
  case build_result::processor_missing:
    return gneiss::engine::error::unsupported;
  case build_result::processor_failed:
    return gneiss::engine::error::dependency_failed;
  case build_result::output_exists:
    return gneiss::engine::error::invalid_state;
  case build_result::io_error:
    return gneiss::engine::error::io;
  }
  return gneiss::engine::error::unknown;
}

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
  stream.close();
  return static_cast<bool>(stream);
}

[[nodiscard]] bool write_asset_build_metadata(const std::filesystem::path& path,
                                              const gneiss::tooling::build_report& report) {
  yyjson_mut_doc* raw_document = yyjson_mut_doc_new(nullptr);
  if (raw_document == nullptr) {
    return false;
  }
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr document(raw_document, &yyjson_mut_doc_free);
  auto* root = yyjson_mut_obj(document.get());
  auto* outputs = yyjson_mut_arr(document.get());
  if (root == nullptr || outputs == nullptr ||
      !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.asset-build") ||
      !yyjson_mut_obj_add_uint(document.get(), root, "version", 1U)) {
    return false;
  }
  for (const auto& output : report.outputs) {
    auto* item = yyjson_mut_obj(document.get());
    auto* dependencies = yyjson_mut_arr(document.get());
    if (item == nullptr || dependencies == nullptr ||
        !yyjson_mut_obj_add_strncpy(document.get(), item, "path", output.relative_path.data(),
                                    output.relative_path.size()) ||
        !yyjson_mut_obj_add_strncpy(document.get(), item, "processor", output.processor_id.data(),
                                    output.processor_id.size()) ||
        !yyjson_mut_obj_add_strncpy(document.get(), item, "cache_key", output.cache_key.data(),
                                    output.cache_key.size())) {
      return false;
    }
    for (const auto& dependency : output.dependencies) {
      if (!yyjson_mut_arr_add_strncpy(document.get(), dependencies, dependency.data(),
                                      dependency.size())) {
        return false;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), item, "dependencies", dependencies) ||
        !yyjson_mut_arr_add_val(outputs, item)) {
      return false;
    }
  }
  if (!yyjson_mut_obj_add_val(document.get(), root, "outputs", outputs)) {
    return false;
  }
  yyjson_mut_doc_set_root(document.get(), root);
  std::size_t length{};
  const std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
  return json != nullptr && write_text(path, std::string_view(json.get(), length));
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

[[nodiscard]] gneiss::engine::error
write_recent_projects(const std::filesystem::path& state_file,
                      const std::vector<editor_project>& projects) {
  std::error_code error;
  std::filesystem::create_directories(state_file.parent_path(), error);
  if (error) {
    return gneiss::engine::error::io;
  }
  yyjson_mut_doc* raw_document = yyjson_mut_doc_new(nullptr);
  if (raw_document == nullptr) {
    return gneiss::engine::error::out_of_memory;
  }
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr document(raw_document, &yyjson_mut_doc_free);
  auto* root = yyjson_mut_obj(document.get());
  auto* recent = yyjson_mut_arr(document.get());
  if (root == nullptr || recent == nullptr ||
      !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.editor-state") ||
      !yyjson_mut_obj_add_uint(document.get(), root, "version", 1U)) {
    return gneiss::engine::error::out_of_memory;
  }
  for (const auto& project : projects) {
    const auto path = path_utf8(project.project_root);
    if (!yyjson_mut_arr_add_strncpy(document.get(), recent, path.data(), path.size())) {
      return gneiss::engine::error::out_of_memory;
    }
  }
  if (!yyjson_mut_obj_add_val(document.get(), root, "recent_projects", recent)) {
    return gneiss::engine::error::out_of_memory;
  }
  yyjson_mut_doc_set_root(document.get(), root);
  std::size_t length = 0;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
  if (!json) {
    return gneiss::engine::error::out_of_memory;
  }
  const auto temporary = state_file.string() + ".tmp";
  if (!write_text(temporary, std::string_view(json.get(), length))) {
    return gneiss::engine::error::io;
  }
  // 同目录替换失败时保留原文件，不能先删除旧配置。
#if defined(_WIN32)
  if (!MoveFileExW(std::filesystem::path(temporary).c_str(), state_file.c_str(),
                   MOVEFILE_REPLACE_EXISTING))
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
  std::filesystem::rename(temporary, state_file, error);
#endif
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
  }
  return error ? gneiss::engine::error::io : gneiss::engine::error::success;
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

gneiss::engine::error load_recent_projects(const std::filesystem::path& state_file,
                                           std::vector<editor_project>& output) noexcept {
  try {
    output.clear();
    std::ifstream stream(state_file, std::ios::binary);
    if (!stream) {
      return gneiss::engine::error::success;
    }
    const std::string json{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    document_ptr document{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG)};
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* recent = yyjson_is_obj(root) ? yyjson_obj_get(root, "recent_projects") : nullptr;
    if (!yyjson_is_arr(recent)) {
      return gneiss::engine::error::invalid_argument;
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
      if (load_editor_project(utf8_path(path), project) == gneiss::engine::error::success) {
        output.push_back(std::move(project));
      }
    }
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::io;
  }
}

gneiss::engine::error remember_recent_project(const std::filesystem::path& state_file,
                                              const editor_project& project) noexcept {
  try {
    std::vector<editor_project> projects;
    const auto load_result = load_recent_projects(state_file, projects);
    if (load_result != gneiss::engine::error::success &&
        load_result != gneiss::engine::error::invalid_argument) {
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
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::io;
  }
}

gneiss::engine::error create_editor_project(const std::filesystem::path& project_root,
                                            std::string_view name,
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
  return gneiss::engine::error::not_found;
#endif
}

gneiss::engine::error create_editor_project(const std::filesystem::path& project_root,
                                            const std::filesystem::path& template_root,
                                            std::string_view name,
                                            editor_project& output) noexcept {
  if (project_root.empty() || name.empty()) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    std::error_code error;
    if (std::filesystem::exists(project_root, error) || error ||
        !std::filesystem::is_directory(template_root, error) || error ||
        !std::filesystem::is_regular_file(template_root / "gneiss.project.json", error) || error) {
      return gneiss::engine::error::invalid_state;
    }
    auto temporary = project_root;
    temporary += ".gneiss-creating";
    if (std::filesystem::exists(temporary, error) || error) {
      return gneiss::engine::error::invalid_state;
    }
    std::filesystem::create_directories(temporary.parent_path(), error);
    if (error) {
      return gneiss::engine::error::io;
    }
    if (!copy_directory(template_root, temporary, error)) {
      return gneiss::engine::error::io;
    }
    std::filesystem::create_directories(temporary / "sources", error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::io;
    }
    yyjson_mut_doc* raw_document = yyjson_mut_doc_new(nullptr);
    if (raw_document == nullptr) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::out_of_memory;
    }
    using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    mutable_document_ptr document(raw_document, &yyjson_mut_doc_free);
    auto* root = yyjson_mut_obj(document.get());
    if (root == nullptr ||
        !yyjson_mut_obj_add_str(document.get(), root, "format", "gneiss.project") ||
        !yyjson_mut_obj_add_uint(document.get(), root, "version", 5U) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "name", name.data(), name.size()) ||
        !yyjson_mut_obj_add_str(document.get(), root, "asset_root", "assets") ||
        !yyjson_mut_obj_add_str(document.get(), root, "startup_scene",
                                "asset://scenes/main.scene.json")) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::out_of_memory;
    }
    auto* asset_build = yyjson_mut_obj(document.get());
    auto* retained_assets = yyjson_mut_arr(document.get());
    if (asset_build == nullptr || retained_assets == nullptr ||
        !yyjson_mut_obj_add_val(document.get(), asset_build, "retain", retained_assets) ||
        !yyjson_mut_obj_add_val(document.get(), root, "asset_build", asset_build)) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t length = 0;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY, &length), &std::free);
    if (!json ||
        !write_text(temporary / "gneiss.project.json", std::string_view(json.get(), length))) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::io;
    }
    std::filesystem::rename(temporary, project_root, error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::io;
    }
    return load_editor_project(project_root, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::io;
  }
}

gneiss::engine::error export_editor_project(const editor_project& project,
                                            const std::filesystem::path& runtime_executable,
                                            const std::filesystem::path& output_root) noexcept {
  return export_editor_project(project, {.runtime_executable = runtime_executable,
                                         .output_root = output_root,
                                         .profile = gneiss::host::project_build_profile::debug,
                                         .create_zip = false,
                                         .asset_progress = {}});
}

gneiss::engine::error export_editor_project(const editor_project& project,
                                            const project_export_options& options) noexcept {
  project_export_report report;
  return export_editor_project(project, options, report);
}

gneiss::engine::error export_editor_project(const editor_project& project,
                                            const project_export_options& options,
                                            project_export_report& report) noexcept {
  report = {};
  if (project.project_root.empty() || options.runtime_executable.empty() ||
      options.output_root.empty()) {
    report.operation = gneiss::engine::error::invalid_argument;
    return gneiss::engine::error::invalid_argument;
  }
  try {
    std::error_code error;
    auto archive = options.output_root;
    archive += ".zip";
    if (std::filesystem::exists(options.output_root, error) || error ||
        (options.create_zip && std::filesystem::exists(archive, error)) || error ||
        !std::filesystem::is_regular_file(options.runtime_executable, error) || error) {
      return gneiss::engine::error::invalid_state;
    }
    auto temporary = options.output_root;
    temporary += ".gneiss-exporting";
    auto temporary_archive = archive;
    temporary_archive += ".gneiss-exporting";
    if (std::filesystem::exists(temporary, error) || error ||
        (options.create_zip && std::filesystem::exists(temporary_archive, error)) || error) {
      return gneiss::engine::error::invalid_state;
    }
    std::filesystem::create_directories(temporary.parent_path(), error);
    if (error) {
      return gneiss::engine::error::io;
    }
    std::filesystem::create_directories(temporary / "bin", error);
    if (error)
      return gneiss::engine::error::io;
    auto root_uris = project.retained_assets;
    root_uris.push_back(project.startup_scene);
    if (!project.input_map.empty()) {
      root_uris.push_back(project.input_map);
    }
    if (!project.environment.asset.empty()) {
      root_uris.push_back(project.environment.asset);
    }
    auto asset_request = gneiss::tooling::build_request{
        .source_root = project.asset_root,
        .output_root = temporary / "assets",
        .cache_root = project.project_root / "build" / ".gneiss-cache",
        .root_uris = std::move(root_uris),
        .target_platform = std::string(platform_name()),
        .target_architecture = std::string(architecture_name()),
        .profile = options.profile == gneiss::host::project_build_profile::shipping
                       ? gneiss::tooling::build_profile::shipping
                       : gneiss::tooling::build_profile::development,
        .progress = {}};
    if (options.asset_progress) {
      asset_request.progress = [&options](const gneiss::tooling::build_progress& progress) {
        options.asset_progress(progress.current, progress.total, progress.relative_path,
                               progress.cache_hit);
      };
    }
    const auto asset_report =
        gneiss::tooling::build_assets(asset_request, gneiss::tooling::make_default_registry());
    report.asset_source_count = asset_report.source_count;
    report.asset_built_count = asset_report.built_count;
    report.asset_cache_hit_count = asset_report.cache_hit_count;
    report.asset_pruned_count = asset_report.pruned_count;
    if (asset_report.result != gneiss::tooling::build_result::success) {
      report.operation = map_asset_build_result(asset_report.result);
      std::filesystem::remove_all(temporary, error);
      return report.operation;
    }
    if (options.profile != gneiss::host::project_build_profile::shipping &&
        !write_asset_build_metadata(temporary / "assets" / ".gneiss-build.json", asset_report)) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::io;
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
      return gneiss::engine::error::io;
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
            return gneiss::engine::error::io;
          }
        }
      }
    }
    const auto runtime_assets = options.runtime_executable.parent_path() / "assets";
    if (std::filesystem::is_directory(runtime_assets, error) && !error &&
        !copy_directory(runtime_assets, temporary / "bin" / "assets", error)) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::io;
    }
    const auto runtime_name = options.runtime_executable.filename().string();
    if (!write_text(temporary / "run.cmd", "@echo off\r\npushd \"%~dp0\"\r\n\"bin\\" +
                                               runtime_name + "\" --project ." +
                                               " %*\r\nset GNEISS_EXIT=%ERRORLEVEL%\r\npopd\r\n"
                                               "exit /b %GNEISS_EXIT%\r\n") ||
        !write_text(temporary / "run.sh",
                    "#!/bin/sh\nDIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
                    "LD_LIBRARY_PATH=\"$DIR/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\" "
                    "\"$DIR/bin/" +
                        runtime_name + "\" --project \"$DIR\"" + " \"$@\"\n")) {
      std::filesystem::remove_all(temporary, error);
      return gneiss::engine::error::io;
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
    if (manifest_result.failed()) {
      std::filesystem::remove_all(temporary, error);
      return manifest_result;
    }
    const auto verify_result = verify_package_manifest(temporary);
    if (verify_result.failed()) {
      std::filesystem::remove_all(temporary, error);
      return verify_result;
    }
    if (options.create_zip) {
      const auto archive_result = write_deterministic_zip(temporary, temporary_archive);
      if (archive_result.failed()) {
        std::filesystem::remove_all(temporary, error);
        std::filesystem::remove(temporary_archive, error);
        return archive_result;
      }
    }
    std::filesystem::rename(temporary, options.output_root, error);
    if (error) {
      std::filesystem::remove_all(temporary, error);
      std::filesystem::remove(temporary_archive, error);
      return gneiss::engine::error::io;
    }
    if (options.create_zip) {
      std::filesystem::rename(temporary_archive, archive, error);
      if (error) {
        std::filesystem::remove_all(options.output_root, error);
        std::filesystem::remove(temporary_archive, error);
        return gneiss::engine::error::io;
      }
    }
    report.operation = gneiss::engine::error::success;
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::io;
  }
}

} // namespace gneiss::host
