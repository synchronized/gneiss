// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_session.h"

#include <gneiss/asset.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <new>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace gneiss::editor {
namespace {

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

[[nodiscard]] result replace_file(const std::filesystem::path& temporary,
                                  const std::filesystem::path& destination) noexcept {
#if defined(_WIN32)
  return MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE
             ? result::success
             : result::io;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return error ? result::io : result::success;
#endif
}

} // namespace

result editor_session::open(gneiss_application application, gneiss_world world,
                            std::string_view uri) noexcept {
  if (application == GNEISS_NULL_APPLICATION || world == GNEISS_NULL_WORLD || uri.empty()) {
    return result::invalid_argument;
  }
  try {
    scene_instance pending;
    auto operation = scene_instance::load(application, uri, pending);
    if (operation != result::success) {
      return operation;
    }
    std::uint64_t count = 0;
    operation = pending.get_node_count(count);
    if (operation != result::success) {
      return operation;
    }
    std::vector<scene_node_record> records;
    records.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
      operation = pending.get_node_info(index, info);
      if (operation != result::success) {
        return operation;
      }
      std::string uuid{info.uuid, static_cast<std::size_t>(info.uuid_length)};
      const auto display_name =
          info.name == nullptr ? uuid
                               : std::string{info.name, static_cast<std::size_t>(info.name_length)};
      records.push_back({scene_node_id{info.node}, scene_node_id{info.parent},
                         entity_id{info.entity}, std::move(uuid), display_name});
    }
    scene_ = std::move(pending);
    nodes_ = std::move(records);
    world_ = world;
    selection_ = {};
    uri_ = uri;
    is_dirty_ = false;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

void editor_session::close() noexcept {
  selection_ = {};
  nodes_.clear();
  uri_.clear();
  is_dirty_ = false;
  scene_.reset();
  world_ = GNEISS_NULL_WORLD;
}

const scene_node_record* editor_session::selected_node() const noexcept {
  const auto found = std::ranges::find(nodes_, selection_, &scene_node_record::node);
  return found == nodes_.end() ? nullptr : &*found;
}

result editor_session::select(scene_node_id node) noexcept {
  if (!node.is_valid()) {
    selection_ = {};
    return result::success;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return result::not_found;
  }
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  const auto operation = from_native(gneiss_scene_node_get_entity(world_, node.get(), &entity));
  if (operation != result::success || entity != found->entity.get()) {
    selection_ = {};
    return operation == result::success ? result::invalid_handle : operation;
  }
  selection_ = node;
  return result::success;
}

result editor_session::validate_selection() noexcept {
  return selection_.is_valid() ? select(selection_) : result::success;
}

result editor_session::save(const std::filesystem::path& asset_root) noexcept {
  if (!is_open() || asset_root.empty() ||
      gneiss_asset_uri_validate(uri_.data(), uri_.size()) != GNEISS_SUCCESS) {
    return result::invalid_argument;
  }
  try {
    constexpr std::string_view scheme = "asset://";
    const auto relative_text = uri_.substr(scheme.size());
    const auto relative = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(relative_text.data()), relative_text.size()));
    std::error_code error;
    const auto canonical_root = std::filesystem::canonical(asset_root, error);
    if (error || !std::filesystem::is_directory(canonical_root, error) || error) {
      return result::io;
    }
    const auto destination = std::filesystem::weakly_canonical(canonical_root / relative, error);
    if (error || !is_within(canonical_root, destination) ||
        !std::filesystem::is_regular_file(destination, error) || error) {
      return result::io;
    }

    std::string json;
    auto operation = scene_.serialize(json);
    if (operation != result::success) {
      return operation;
    }
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = destination;
    temporary += ".gneiss-" + std::to_string(suffix) + ".tmp";
    {
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      stream.write(json.data(), static_cast<std::streamsize>(json.size()));
      stream.flush();
      if (!stream) {
        stream.close();
        std::filesystem::remove(temporary, error);
        return result::io;
      }
    }
    operation = replace_file(temporary, destination);
    if (operation != result::success) {
      std::filesystem::remove(temporary, error);
      return operation;
    }
    is_dirty_ = false;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
