// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/app/project_description.h>

#include <gneiss/asset.h>

#include <yyjson.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <set>
#include <string_view>
#include <system_error>

namespace gneiss::app {
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

[[nodiscard]] bool read_optional_string(yyjson_val* object, const char* key, std::string& output) {
  auto* value = yyjson_obj_get(object, key);
  if (value == nullptr) {
    output.clear();
    return true;
  }
  if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U) {
    return false;
  }
  output.assign(yyjson_get_str(value), yyjson_get_len(value));
  return true;
}

[[nodiscard]] bool read_number(yyjson_val* object, const char* key, float& output) noexcept {
  auto* value = yyjson_obj_get(object, key);
  if (!yyjson_is_num(value)) {
    return false;
  }
  output = static_cast<float>(yyjson_get_num(value));
  return std::isfinite(output);
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

[[nodiscard]] bool valid_identifier(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }
  for (const auto character : value) {
    if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '_' || character == '-')) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] result fail(project_load_report& report, project_load_stage stage, result operation,
                          const std::filesystem::path& context) noexcept {
  report.operation = operation;
  report.stage = stage;
  try {
    report.context = context;
  } catch (...) {
    report.context.clear();
  }
  return operation;
}

[[nodiscard]] bool read_profile(yyjson_val* profiles, const char* key,
                                game_build_profile_description& output) {
  auto* profile = yyjson_obj_get(profiles, key);
  std::string directory;
  if (!yyjson_is_obj(profile) || !read_string(profile, "directory", directory) ||
      !read_string(profile, "configure_preset", output.configure_preset) ||
      !read_string(profile, "build_preset", output.build_preset) ||
      !valid_relative_directory(directory) || !valid_identifier(output.configure_preset) ||
      !valid_identifier(output.build_preset)) {
    return false;
  }
  output.directory = utf8_path(directory);
  return true;
}

[[nodiscard]] bool read_retained_assets(yyjson_val* asset_build, std::vector<std::string>& output) {
  auto* retained = yyjson_obj_get(asset_build, "retain");
  if (!yyjson_is_arr(retained)) {
    return false;
  }
  std::set<std::string> unique;
  yyjson_val* item = nullptr;
  std::size_t index = 0U;
  std::size_t maximum = 0U;
  yyjson_arr_foreach(retained, index, maximum, item) {
    if (!yyjson_is_str(item)) {
      return false;
    }
    std::string uri(yyjson_get_str(item), yyjson_get_len(item));
    if (gneiss_asset_uri_validate(uri.data(), uri.size()) != GNEISS_SUCCESS ||
        !unique.insert(uri).second) {
      return false;
    }
    output.push_back(std::move(uri));
  }
  return true;
}

} // namespace

std::string_view game_build_profile_name(game_build_profile profile) noexcept {
  switch (profile) {
  case game_build_profile::debug:
    return "debug";
  case game_build_profile::development:
    return "development";
  case game_build_profile::shipping:
    return "shipping";
  }
  return "unknown";
}

const game_build_profile_description&
game_build_profile_description_for(const game_module_description& module,
                                   game_build_profile profile) noexcept {
  return module.profiles[static_cast<std::size_t>(profile)];
}

std::string_view project_load_stage_name(project_load_stage stage) noexcept {
  switch (stage) {
  case project_load_stage::none:
    return "none";
  case project_load_stage::argument:
    return "argument";
  case project_load_stage::project_root:
    return "project_root";
  case project_load_stage::project_file:
    return "project_file";
  case project_load_stage::document:
    return "document";
  case project_load_stage::schema:
    return "schema";
  case project_load_stage::asset_root:
    return "asset_root";
  case project_load_stage::startup_scene:
    return "startup_scene";
  case project_load_stage::input_map:
    return "input_map";
  case project_load_stage::environment:
    return "environment";
  case project_load_stage::asset_build:
    return "asset_build";
  case project_load_stage::game_module:
    return "game_module";
  }
  return "unknown";
}

result resolve_game_module_path(const project_description& project,
                                std::filesystem::path& output) noexcept {
  return resolve_game_module_path(project, game_build_profile::debug, output);
}

result resolve_game_module_path(const project_description& project, game_build_profile profile,
                                std::filesystem::path& output) noexcept {
  output.clear();
  const auto& build = game_build_profile_description_for(project.game_module, profile);
  if (project.project_root.empty() || project.game_module.name.empty() || build.directory.empty()) {
    return result::invalid_argument;
  }
  try {
#if defined(_WIN32)
    const auto filename = project.game_module.name + ".dll";
#elif defined(__APPLE__)
    const auto filename = "lib" + project.game_module.name + ".dylib";
#elif defined(__linux__) || defined(__unix__)
    const auto filename = "lib" + project.game_module.name + ".so";
#else
    return result::unsupported;
#endif
    std::error_code error;
    const auto candidate =
        std::filesystem::weakly_canonical(project.project_root / build.directory / filename, error);
    if (error || !is_within(project.project_root, candidate)) {
      return result::invalid_argument;
    }
    if (!std::filesystem::is_regular_file(candidate, error) || error) {
      return result::not_found;
    }
    output = candidate;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result load_project_description(const std::filesystem::path& project_root,
                                project_description& output, project_load_report& report) noexcept {
  report = {};
  if (project_root.empty()) {
    return fail(report, project_load_stage::argument, result::invalid_argument, project_root);
  }
  try {
    std::error_code error;
    if (!std::filesystem::exists(project_root, error) || error) {
      return fail(report, project_load_stage::project_root, result::not_found, project_root);
    }
    if (!std::filesystem::is_directory(project_root, error) || error) {
      return fail(report, project_load_stage::project_root, result::invalid_argument, project_root);
    }
    const auto canonical_root = std::filesystem::canonical(project_root, error);
    if (error) {
      return fail(report, project_load_stage::project_root, result::not_found, project_root);
    }
    const auto project_file = canonical_root / project_filename;
    if (!std::filesystem::is_regular_file(project_file, error) || error) {
      return fail(report, project_load_stage::project_file, result::not_found, project_file);
    }

    std::ifstream stream(project_file, std::ios::binary);
    if (!stream) {
      return fail(report, project_load_stage::project_file, result::io, project_file);
    }
    const std::string json{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    if (stream.bad() || json.empty()) {
      return fail(report, project_load_stage::document, result::io, project_file);
    }
    document_ptr document{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG)};
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(root)) {
      return fail(report, project_load_stage::document, result::invalid_argument, project_file);
    }
    auto* format = yyjson_obj_get(root, "format");
    auto* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_str(format) ||
        std::string_view(yyjson_get_str(format), yyjson_get_len(format)) != "gneiss.project" ||
        !yyjson_is_uint(version)) {
      return fail(report, project_load_stage::schema, result::invalid_argument, project_file);
    }
    const auto format_version = yyjson_get_uint(version);
    if (format_version < 1U || format_version > 5U) {
      return fail(report, project_load_stage::schema, result::unsupported, project_file);
    }
    project_description pending;
    std::string asset_root_text;
    if (!read_string(root, "name", pending.name) ||
        !read_string(root, "asset_root", asset_root_text) ||
        !read_string(root, "startup_scene", pending.startup_scene) ||
        !valid_relative_directory(asset_root_text) ||
        gneiss_asset_uri_validate(pending.startup_scene.data(), pending.startup_scene.size()) !=
            GNEISS_SUCCESS) {
      return fail(report, project_load_stage::schema, result::invalid_argument, project_file);
    }

    auto* game_module = yyjson_obj_get(root, "game_module");
    if (game_module != nullptr) {
      if (format_version < 2U || !yyjson_is_obj(game_module) ||
          !read_string(game_module, "name", pending.game_module.name) ||
          !read_string(game_module, "build_target", pending.game_module.build_target) ||
          !valid_identifier(pending.game_module.name) ||
          !valid_identifier(pending.game_module.build_target)) {
        return fail(report, project_load_stage::game_module, result::invalid_argument,
                    project_file);
      }
      auto* profiles = yyjson_obj_get(game_module, "profiles");
      if (profiles != nullptr) {
        if (format_version < 4U || !yyjson_is_obj(profiles) ||
            !read_profile(profiles, "debug", pending.game_module.profiles[0]) ||
            !read_profile(profiles, "development", pending.game_module.profiles[1]) ||
            !read_profile(profiles, "shipping", pending.game_module.profiles[2])) {
          return fail(report, project_load_stage::game_module, result::invalid_argument,
                      project_file);
        }
      } else {
        std::string directory;
        auto& debug = pending.game_module.profiles[0];
        if (format_version >= 4U || !read_string(game_module, "directory", directory) ||
            !read_string(game_module, "configure_preset", debug.configure_preset) ||
            !read_string(game_module, "build_preset", debug.build_preset) ||
            !valid_relative_directory(directory) || !valid_identifier(debug.configure_preset) ||
            !valid_identifier(debug.build_preset)) {
          return fail(report, project_load_stage::game_module, result::invalid_argument,
                      project_file);
        }
        debug.directory = utf8_path(directory);
      }
    }

    if (!read_optional_string(root, "input_map", pending.input_map) ||
        (!pending.input_map.empty() &&
         gneiss_asset_uri_validate(pending.input_map.data(), pending.input_map.size()) !=
             GNEISS_SUCCESS)) {
      return fail(report, project_load_stage::input_map, result::invalid_argument, project_file);
    }

    auto* environment = yyjson_obj_get(root, "environment");
    if (environment != nullptr) {
      if (format_version < 3U || !yyjson_is_obj(environment) ||
          !read_optional_string(environment, "asset", pending.environment.asset) ||
          !read_number(environment, "intensity", pending.environment.intensity) ||
          !read_number(environment, "rotation_degrees", pending.environment.rotation_degrees) ||
          pending.environment.intensity < 0.0F ||
          (!pending.environment.asset.empty() &&
           gneiss_asset_uri_validate(pending.environment.asset.data(),
                                     pending.environment.asset.size()) != GNEISS_SUCCESS)) {
        return fail(report, project_load_stage::environment, result::invalid_argument,
                    project_file);
      }
    }

    auto* asset_build = yyjson_obj_get(root, "asset_build");
    if ((format_version >= 5U && !yyjson_is_obj(asset_build)) ||
        (asset_build != nullptr && (format_version < 5U || !yyjson_is_obj(asset_build) ||
                                    !read_retained_assets(asset_build, pending.retained_assets)))) {
      return fail(report, project_load_stage::asset_build, result::invalid_argument, project_file);
    }

    pending.project_file = project_file;
    pending.project_root = canonical_root;
    pending.asset_root =
        std::filesystem::weakly_canonical(pending.project_root / utf8_path(asset_root_text), error);
    if (error || !is_within(pending.project_root, pending.asset_root) ||
        !std::filesystem::is_directory(pending.asset_root, error) || error) {
      return fail(report, project_load_stage::asset_root, result::not_found,
                  pending.project_root / utf8_path(asset_root_text));
    }
    constexpr std::string_view scheme = "asset://";
    const auto scene_path = std::filesystem::weakly_canonical(
        pending.asset_root /
            utf8_path(std::string_view(pending.startup_scene).substr(scheme.size())),
        error);
    if (error || !is_within(pending.asset_root, scene_path) ||
        !std::filesystem::is_regular_file(scene_path, error) || error) {
      return fail(report, project_load_stage::startup_scene, result::not_found, scene_path);
    }
    if (!pending.input_map.empty()) {
      const auto input_map_path = std::filesystem::weakly_canonical(
          pending.asset_root / utf8_path(std::string_view(pending.input_map).substr(scheme.size())),
          error);
      if (error || !is_within(pending.asset_root, input_map_path) ||
          !std::filesystem::is_regular_file(input_map_path, error) || error) {
        return fail(report, project_load_stage::input_map, result::not_found, input_map_path);
      }
    }
    if (!pending.environment.asset.empty()) {
      const auto environment_path = std::filesystem::weakly_canonical(
          pending.asset_root /
              utf8_path(std::string_view(pending.environment.asset).substr(scheme.size())),
          error);
      if (error || !is_within(pending.asset_root, environment_path) ||
          !std::filesystem::is_regular_file(environment_path, error) || error) {
        return fail(report, project_load_stage::environment, result::not_found, environment_path);
      }
    }
    output = std::move(pending);
    report = {};
    return result::success;
  } catch (const std::bad_alloc&) {
    return fail(report, project_load_stage::none, result::out_of_memory, project_root);
  } catch (...) {
    return fail(report, project_load_stage::none, result::io, project_root);
  }
}

} // namespace gneiss::app
