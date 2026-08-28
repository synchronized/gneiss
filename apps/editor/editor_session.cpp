// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_session.h"

#include <gneiss/asset.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <new>
#include <random>
#include <sstream>
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
    scene_ = std::move(pending);
    world_ = world;
    selection_ = {};
    uri_ = uri;
    is_dirty_ = false;
    return refresh_nodes();
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::create_empty(gneiss_application application, gneiss_world world,
                                    std::string_view scene_uuid) noexcept {
  if (application == GNEISS_NULL_APPLICATION || world == GNEISS_NULL_WORLD || scene_uuid.empty()) {
    return result::invalid_argument;
  }
  scene_instance pending;
  const auto operation = scene_instance::create_empty(application, scene_uuid, pending);
  if (operation != result::success) {
    return operation;
  }
  scene_ = std::move(pending);
  world_ = world;
  nodes_.clear();
  selection_ = {};
  uri_.clear();
  is_dirty_ = true;
  return result::success;
}

result editor_session::refresh_nodes() noexcept {
  try {
    std::uint64_t count = 0;
    auto operation = scene_.get_node_count(count);
    if (operation != result::success) {
      return operation;
    }
    std::vector<scene_node_record> records;
    records.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
      operation = scene_.get_node_info(index, info);
      if (operation != result::success) {
        return operation;
      }
      std::string uuid{info.uuid, static_cast<std::size_t>(info.uuid_length)};
      records.push_back(
          {.node = scene_node_id{info.node},
           .parent = scene_node_id{info.parent},
           .entity = entity_id{info.entity},
           .uuid = uuid,
           .display_name = info.name == nullptr
                               ? std::move(uuid)
                               : std::string{info.name, static_cast<std::size_t>(info.name_length)},
           .mesh_uri =
               info.mesh_uri == nullptr
                   ? std::string{}
                   : std::string{info.mesh_uri, static_cast<std::size_t>(info.mesh_uri_length)},
           .material_uri = info.material_uri == nullptr
                               ? std::string{}
                               : std::string{info.material_uri,
                                             static_cast<std::size_t>(info.material_uri_length)},
           .component_flags = info.component_flags,
           .camera = info.camera,
           .is_primary_camera =
               (info.component_flags & GNEISS_SCENE_NODE_COMPONENT_PRIMARY_CAMERA) != 0U});
    }
    nodes_.swap(records);
    return validate_selection();
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::create_node(std::string_view name, scene_node_id parent,
                                   scene_node_id& out_node) noexcept {
  if (!is_open()) {
    return result::invalid_state;
  }
  try {
    const auto uuid = make_uuid();
    scene_node_desc desc = GNEISS_SCENE_NODE_DESC_INIT;
    desc.uuid = uuid.data();
    desc.uuid_length = uuid.size();
    desc.name = name.data();
    desc.name_length = name.size();
    desc.parent = parent.get();
    auto operation = scene_.create_node(desc, out_node);
    if (operation == result::success) {
      selection_ = out_node;
      operation = refresh_nodes();
    }
    if (operation == result::success) {
      is_dirty_ = true;
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::rename_node(scene_node_id node, std::string_view name) noexcept {
  if (!is_open() || !node.is_valid()) {
    return result::invalid_argument;
  }
  const auto operation = scene_.set_node_name(node, name);
  if (operation != result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

result editor_session::reparent_node(scene_node_id node, scene_node_id parent) noexcept {
  if (!is_open() || !node.is_valid() || node == parent) {
    return result::invalid_argument;
  }
  const auto operation = scene_.reparent_node(node, parent);
  if (operation != result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

result editor_session::destroy_subtree(scene_node_id node,
                                       scene_subtree_snapshot& out_snapshot) noexcept {
  if (!is_open() || !node.is_valid()) {
    return result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return result::not_found;
  }
  try {
    scene_subtree_snapshot snapshot{.root_uuid = found->uuid};
    if (found->parent.is_valid()) {
      const auto parent = std::ranges::find(nodes_, found->parent, &scene_node_record::node);
      if (parent == nodes_.end()) {
        return result::invalid_state;
      }
      snapshot.parent_uuid = parent->uuid;
    }
    auto operation = scene_.capture_subtree(node, snapshot.json);
    if (operation != result::success) {
      return operation;
    }
    operation = scene_.destroy_subtree(node);
    if (operation == result::success) {
      selection_ = {};
      operation = refresh_nodes();
    }
    if (operation == result::success) {
      out_snapshot = std::move(snapshot);
      is_dirty_ = true;
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::restore_subtree(const scene_subtree_snapshot& snapshot,
                                       scene_node_id& out_node) noexcept {
  scene_node_id parent;
  if (!snapshot.parent_uuid.empty()) {
    const auto* record = find_node(snapshot.parent_uuid);
    if (record == nullptr) {
      return result::not_found;
    }
    parent = record->node;
  }
  const auto operation = scene_.restore_subtree(snapshot.json, parent, {}, out_node);
  if (operation != result::success) {
    return operation;
  }
  selection_ = out_node;
  const auto refreshed = refresh_nodes();
  if (refreshed == result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

result editor_session::duplicate_subtree(scene_node_id node, scene_node_id parent,
                                         scene_node_id& out_node) noexcept {
  if (!is_open() || !node.is_valid()) {
    return result::invalid_argument;
  }
  try {
    std::string snapshot;
    auto operation = scene_.capture_subtree(node, snapshot);
    if (operation != result::success) {
      return operation;
    }
    std::vector<std::string> sources;
    std::vector<std::string> targets;
    for (const auto& candidate : nodes_) {
      auto current = candidate.node;
      while (current.is_valid()) {
        if (current == node) {
          sources.push_back(candidate.uuid);
          targets.push_back(make_uuid());
          break;
        }
        const auto ancestor = std::ranges::find(nodes_, current, &scene_node_record::node);
        current = ancestor == nodes_.end() ? scene_node_id{} : ancestor->parent;
      }
    }
    std::vector<scene_uuid_mapping> mappings;
    mappings.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
      mappings.push_back({.source_uuid = sources[index].data(),
                          .source_uuid_length = sources[index].size(),
                          .target_uuid = targets[index].data(),
                          .target_uuid_length = targets[index].size()});
    }
    operation = scene_.restore_subtree(snapshot, parent, mappings, out_node);
    if (operation == result::success) {
      selection_ = out_node;
      operation = refresh_nodes();
    }
    if (operation == result::success) {
      is_dirty_ = true;
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::set_camera(scene_node_id node, const scene_camera_desc& desc) noexcept {
  const auto operation = scene_.set_camera(node, desc);
  if (operation != result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

result editor_session::remove_camera(scene_node_id node) noexcept {
  const auto operation = scene_.remove_camera(node);
  if (operation != result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

result editor_session::remove_mesh_renderer(scene_node_id node) noexcept {
  const auto operation = scene_.remove_mesh_renderer(node);
  if (operation != result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == result::success) {
    is_dirty_ = true;
  }
  return refreshed;
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

const scene_node_record* editor_session::find_node(std::string_view uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, uuid, &scene_node_record::uuid);
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

result editor_session::create_mesh_renderer_node(std::string_view name, std::string_view mesh_uri,
                                                 std::string_view material_uri,
                                                 scene_node_id& out_node) noexcept {
  if (!is_open() || mesh_uri.empty() || material_uri.empty()) {
    return result::invalid_argument;
  }
  try {
    const auto uuid = make_uuid();
    return create_mesh_renderer_node({.uuid = uuid,
                                      .parent_uuid = {},
                                      .display_name = name.empty() ? uuid : std::string{name},
                                      .mesh_uri = std::string{mesh_uri},
                                      .material_uri = std::string{material_uri}},
                                     out_node);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::create_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                                 scene_node_id& out_node) noexcept {
  if (!is_open() || snapshot.uuid.empty() || snapshot.mesh_uri.empty() ||
      snapshot.material_uri.empty()) {
    return result::invalid_argument;
  }
  try {
    scene_node_id parent;
    if (!snapshot.parent_uuid.empty()) {
      const auto* parent_record = find_node(snapshot.parent_uuid);
      if (parent_record == nullptr) {
        return result::not_found;
      }
      parent = parent_record->node;
    }
    nodes_.reserve(nodes_.size() + 1U);
    scene_mesh_renderer_node_desc desc = GNEISS_SCENE_MESH_RENDERER_NODE_DESC_INIT;
    desc.uuid = snapshot.uuid.data();
    desc.uuid_length = snapshot.uuid.size();
    desc.name = snapshot.display_name.empty() ? nullptr : snapshot.display_name.data();
    desc.name_length = snapshot.display_name.size();
    desc.parent = parent.get();
    desc.renderer.mesh_uri = snapshot.mesh_uri.data();
    desc.renderer.mesh_uri_length = snapshot.mesh_uri.size();
    desc.renderer.material_uri = snapshot.material_uri.data();
    desc.renderer.material_uri_length = snapshot.material_uri.size();
    auto operation = scene_.create_mesh_renderer_node(desc, out_node);
    if (operation != result::success) {
      return operation;
    }
    std::uint64_t count = 0;
    scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
    operation = scene_.get_node_count(count);
    if (operation == result::success && count > 0U) {
      operation = scene_.get_node_info(count - 1U, info);
    }
    if (operation != result::success) {
      return operation;
    }
    nodes_.push_back(
        {.node = scene_node_id{info.node},
         .parent = scene_node_id{info.parent},
         .entity = entity_id{info.entity},
         .uuid = snapshot.uuid,
         .display_name = snapshot.display_name.empty() ? snapshot.uuid : snapshot.display_name,
         .mesh_uri = snapshot.mesh_uri,
         .material_uri = snapshot.material_uri});
    selection_ = out_node;
    is_dirty_ = true;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::restore_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                                  scene_node_id& out_node) noexcept {
  return create_mesh_renderer_node(snapshot, out_node);
}

result editor_session::destroy_node(scene_node_id node,
                                    scene_node_snapshot& out_snapshot) noexcept {
  if (!is_open() || !node.is_valid()) {
    return result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return result::not_found;
  }
  if (found->mesh_uri.empty() || found->material_uri.empty()) {
    return result::unsupported;
  }
  if (std::ranges::any_of(nodes_,
                          [node](const auto& candidate) { return candidate.parent == node; })) {
    return result::invalid_state;
  }
  try {
    scene_node_snapshot snapshot{.uuid = found->uuid,
                                 .parent_uuid = {},
                                 .display_name = found->display_name,
                                 .mesh_uri = found->mesh_uri,
                                 .material_uri = found->material_uri};
    if (found->parent.is_valid()) {
      const auto parent = std::ranges::find(nodes_, found->parent, &scene_node_record::node);
      if (parent == nodes_.end()) {
        return result::invalid_state;
      }
      snapshot.parent_uuid = parent->uuid;
    }
    const auto operation = scene_.destroy_node(node);
    if (operation != result::success) {
      return operation;
    }
    nodes_.erase(found);
    if (selection_ == node) {
      selection_ = {};
    }
    out_snapshot = std::move(snapshot);
    is_dirty_ = true;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                         std::string_view material_uri) noexcept {
  if (!is_open() || !node.is_valid() || mesh_uri.empty() || material_uri.empty()) {
    return result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return result::not_found;
  }
  try {
    std::string mesh(mesh_uri);
    std::string material(material_uri);
    scene_mesh_renderer_desc desc = GNEISS_SCENE_MESH_RENDERER_DESC_INIT;
    desc.mesh_uri = mesh.data();
    desc.mesh_uri_length = mesh.size();
    desc.material_uri = material.data();
    desc.material_uri_length = material.size();
    const auto operation = scene_.set_mesh_renderer(node, desc);
    if (operation != result::success) {
      return operation;
    }
    found->mesh_uri.swap(mesh);
    found->material_uri.swap(material);
    is_dirty_ = true;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_session::save(const std::filesystem::path& asset_root) noexcept {
  return save_to(asset_root, uri_, true);
}

result editor_session::save_as(const std::filesystem::path& asset_root,
                               std::string_view uri) noexcept {
  return save_to(asset_root, uri, false);
}

result editor_session::save_to(const std::filesystem::path& asset_root, std::string_view uri,
                               bool require_existing) noexcept {
  if (!is_open() || asset_root.empty() ||
      gneiss_asset_uri_validate(uri.data(), uri.size()) != GNEISS_SUCCESS) {
    return result::invalid_argument;
  }
  try {
    const std::string target_uri{uri};
    constexpr std::string_view scheme = "asset://";
    const auto relative_text = target_uri.substr(scheme.size());
    const auto relative = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(relative_text.data()), relative_text.size()));
    std::error_code error;
    const auto canonical_root = std::filesystem::canonical(asset_root, error);
    if (error || !std::filesystem::is_directory(canonical_root, error) || error) {
      return result::io;
    }
    const auto destination = std::filesystem::weakly_canonical(canonical_root / relative, error);
    if (error || !is_within(canonical_root, destination)) {
      return result::io;
    }
    const auto exists = std::filesystem::exists(destination, error);
    if (error ||
        (require_existing && (!exists || !std::filesystem::is_regular_file(destination, error))) ||
        (!require_existing && exists) || error ||
        !std::filesystem::is_directory(destination.parent_path(), error) || error) {
      return exists && !require_existing ? result::invalid_state : result::io;
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
    uri_ = target_uri;
    is_dirty_ = false;
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

} // namespace gneiss::editor
