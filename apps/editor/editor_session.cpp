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
      records.push_back(
          {scene_node_id{info.node}, scene_node_id{info.parent}, entity_id{info.entity},
           std::move(uuid), display_name,
           info.mesh_uri == nullptr
               ? std::string{}
               : std::string{info.mesh_uri, static_cast<std::size_t>(info.mesh_uri_length)},
           info.material_uri == nullptr
               ? std::string{}
               : std::string{info.material_uri,
                             static_cast<std::size_t>(info.material_uri_length)}});
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
