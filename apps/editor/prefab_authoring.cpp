// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "prefab_authoring.h"

#include <gneiss/asset.h>

#include <yyjson.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace gneiss::editor {
namespace {

struct document_deleter final {
  void operator()(yyjson_doc* document) const noexcept { yyjson_doc_free(document); }
};

using document_ptr = std::unique_ptr<yyjson_doc, document_deleter>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

[[nodiscard]] bool is_canonical_uuid(std::string_view value) noexcept {
  if (value.size() != 36U || value[8] != '-' || value[13] != '-' || value[18] != '-' ||
      value[23] != '-' || value[14] != '4' ||
      (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b')) {
    return false;
  }
  for (std::size_t index = 0U; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      continue;
    }
    const bool is_digit = value[index] >= '0' && value[index] <= '9';
    const bool is_lower_hex = value[index] >= 'a' && value[index] <= 'f';
    if (!is_digit && !is_lower_hex) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::string_view> string_member(yyjson_val* object,
                                                            const char* name) noexcept {
  auto* value = yyjson_obj_get(object, name);
  if (!yyjson_is_str(value)) {
    return std::nullopt;
  }
  return std::string_view{yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] bool contains_uuid(yyjson_val* values, const char* member,
                                 std::string_view target) noexcept {
  for (std::size_t index = 0U; index < yyjson_arr_size(values); ++index) {
    const auto uuid = string_member(yyjson_arr_get(values, index), member);
    if (uuid && *uuid == target) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool put_string(yyjson_mut_doc* document, yyjson_mut_val* object, const char* name,
                              std::string_view value) noexcept {
  return yyjson_mut_obj_put(object, yyjson_mut_strcpy(document, name),
                            yyjson_mut_strncpy(document, value.data(), value.size()));
}

[[nodiscard]] yyjson_mut_val* make_identity_transform(yyjson_mut_doc* document) noexcept {
  auto* transform = yyjson_mut_obj(document);
  auto* translation = yyjson_mut_arr(document);
  auto* rotation = yyjson_mut_arr(document);
  auto* scale = yyjson_mut_arr(document);
  if (transform == nullptr || translation == nullptr || rotation == nullptr || scale == nullptr) {
    return nullptr;
  }
  const std::array translation_values{0.0, 0.0, 0.0};
  const std::array rotation_values{0.0, 0.0, 0.0, 1.0};
  const std::array scale_values{1.0, 1.0, 1.0};
  for (const auto value : translation_values) {
    if (!yyjson_mut_arr_add_real(document, translation, value)) {
      return nullptr;
    }
  }
  for (const auto value : rotation_values) {
    if (!yyjson_mut_arr_add_real(document, rotation, value)) {
      return nullptr;
    }
  }
  for (const auto value : scale_values) {
    if (!yyjson_mut_arr_add_real(document, scale, value)) {
      return nullptr;
    }
  }
  if (!yyjson_mut_obj_add_val(document, transform, "translation", translation) ||
      !yyjson_mut_obj_add_val(document, transform, "rotation", rotation) ||
      !yyjson_mut_obj_add_val(document, transform, "scale", scale)) {
    return nullptr;
  }
  return transform;
}

[[nodiscard]] result write_document(yyjson_mut_doc* document, std::string& output) {
  std::size_t length = 0U;
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr text(yyjson_mut_write(document, YYJSON_WRITE_PRETTY, &length), &std::free);
  if (!text) {
    return result::out_of_memory;
  }
  output.assign(text.get(), length);
  return result::success;
}

[[nodiscard]] result collect_subtree(yyjson_val* objects, std::string_view root_uuid,
                                     std::unordered_set<std::string>& included,
                                     yyjson_val*& root_object) {
  const auto count = yyjson_arr_size(objects);
  for (std::size_t index = 0U; index < count; ++index) {
    auto* object = yyjson_arr_get(objects, index);
    const auto uuid = string_member(object, "uuid");
    if (!uuid) {
      return result::invalid_argument;
    }
    if (*uuid == root_uuid) {
      if (root_object != nullptr) {
        return result::invalid_argument;
      }
      root_object = object;
      included.emplace(*uuid);
    }
  }
  if (root_object == nullptr) {
    return result::not_found;
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0U; index < count; ++index) {
      auto* object = yyjson_arr_get(objects, index);
      auto* parent = yyjson_obj_get(object, "parent");
      const auto uuid = string_member(object, "uuid");
      if (uuid && yyjson_is_str(parent) &&
          included.contains(std::string{yyjson_get_str(parent),
                                        static_cast<std::size_t>(yyjson_get_len(parent))}) &&
          included.emplace(*uuid).second) {
        changed = true;
      }
    }
  }
  return result::success;
}

[[nodiscard]] bool contains_nested_prefab(yyjson_val* instances,
                                          const std::unordered_set<std::string>& included) {
  std::size_t index = 0U;
  std::size_t maximum = 0U;
  yyjson_val* instance = nullptr;
  yyjson_arr_foreach(instances, index, maximum, instance) {
    auto* parent = yyjson_obj_get(instance, "parent");
    if (yyjson_is_str(parent) &&
        included.contains(std::string{yyjson_get_str(parent),
                                      static_cast<std::size_t>(yyjson_get_len(parent))})) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] result make_prefab_document(yyjson_doc* source,
                                          const std::unordered_set<std::string>& included,
                                          const create_prefab_author_request& request,
                                          std::string& output) {
  mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
  auto* prefab_envelope = document ? yyjson_mut_obj(document.get()) : nullptr;
  auto* prefab_objects = document ? yyjson_mut_arr(document.get()) : nullptr;
  auto* source_root = yyjson_doc_get_root(source);
  auto* source_objects = yyjson_obj_get(source_root, "objects");
  yyjson_mut_val* prefab_root = nullptr;
  for (std::size_t index = 0U; index < yyjson_arr_size(source_objects); ++index) {
    auto* source_object = yyjson_arr_get(source_objects, index);
    const auto uuid = string_member(source_object, "uuid");
    if (!uuid || !included.contains(std::string{*uuid})) {
      continue;
    }
    auto* object = yyjson_val_mut_copy(document.get(), source_object);
    if (object == nullptr || !yyjson_mut_arr_add_val(prefab_objects, object)) {
      return result::out_of_memory;
    }
    if (*uuid == request.root_uuid) {
      prefab_root = object;
    }
  }
  auto* identity = make_identity_transform(document.get());
  if (prefab_root == nullptr || identity == nullptr ||
      !yyjson_mut_obj_put(prefab_root, yyjson_mut_strcpy(document.get(), "parent"),
                          yyjson_mut_null(document.get())) ||
      !yyjson_mut_obj_put(prefab_root, yyjson_mut_strcpy(document.get(), "transform"), identity) ||
      !put_string(document.get(), prefab_envelope, "format", "gneiss.prefab") ||
      !yyjson_mut_obj_put(prefab_envelope, yyjson_mut_strcpy(document.get(), "version"),
                          yyjson_mut_uint(document.get(), 1U)) ||
      !put_string(document.get(), prefab_envelope, "prefab_uuid", request.prefab_uuid) ||
      !yyjson_mut_obj_add_val(document.get(), prefab_envelope, "objects", prefab_objects)) {
    return result::out_of_memory;
  }
  yyjson_mut_doc_set_root(document.get(), prefab_envelope);
  return write_document(document.get(), output);
}

[[nodiscard]] result make_scene_document(yyjson_doc* source,
                                         const std::unordered_set<std::string>& included,
                                         yyjson_val* source_root,
                                         const create_prefab_author_request& request,
                                         std::string& output) {
  mutable_document_ptr document(yyjson_doc_mut_copy(source, nullptr), &yyjson_mut_doc_free);
  auto* root = document ? yyjson_mut_doc_get_root(document.get()) : nullptr;
  auto* objects = yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "objects") : nullptr;
  auto* instances =
      yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "prefab_instances") : nullptr;
  if (!yyjson_mut_is_arr(objects) || !yyjson_mut_is_arr(instances)) {
    return result::invalid_argument;
  }
  for (std::size_t index = yyjson_mut_arr_size(objects); index != 0U; --index) {
    auto* object = yyjson_mut_arr_get(objects, index - 1U);
    auto* uuid = yyjson_mut_obj_get(object, "uuid");
    if (yyjson_mut_is_str(uuid) &&
        included.contains(std::string{yyjson_mut_get_str(uuid),
                                      static_cast<std::size_t>(yyjson_mut_get_len(uuid))})) {
      (void)yyjson_mut_arr_remove(objects, index - 1U);
    }
  }

  auto* instance = yyjson_mut_obj(document.get());
  auto* source_parent = yyjson_obj_get(source_root, "parent");
  auto* source_transform = yyjson_obj_get(source_root, "transform");
  const auto name = string_member(source_root, "name").value_or(request.root_uuid);
  auto* parent = yyjson_val_mut_copy(document.get(), source_parent);
  auto* transform = yyjson_val_mut_copy(document.get(), source_transform);
  auto* overrides = yyjson_mut_arr(document.get());
  if (instance == nullptr || parent == nullptr || transform == nullptr || overrides == nullptr ||
      !put_string(document.get(), instance, "instance_uuid", request.instance_uuid) ||
      !put_string(document.get(), instance, "name", name) ||
      !yyjson_mut_obj_add_val(document.get(), instance, "parent", parent) ||
      !put_string(document.get(), instance, "prefab", request.prefab_uri) ||
      !yyjson_mut_obj_add_val(document.get(), instance, "transform", transform) ||
      !yyjson_mut_obj_add_val(document.get(), instance, "overrides", overrides) ||
      !yyjson_mut_arr_add_val(instances, instance)) {
    return result::out_of_memory;
  }
  return write_document(document.get(), output);
}

} // namespace

result prepare_create_prefab(std::string_view scene_json,
                             const create_prefab_author_request& request,
                             std::vector<author_document_change>& out_changes) noexcept {
  out_changes.clear();
  if (scene_json.empty() || request.scene_path.empty() || request.prefab_path.empty() ||
      request.scene_path == request.prefab_path || !is_canonical_uuid(request.root_uuid) ||
      !is_canonical_uuid(request.prefab_uuid) || !is_canonical_uuid(request.instance_uuid) ||
      gneiss_asset_uri_validate(request.prefab_uri.data(), request.prefab_uri.size()) !=
          GNEISS_SUCCESS) {
    return result::invalid_argument;
  }
  try {
    if (request.prefab_uri != std::string{"asset://"} + std::string{request.prefab_path}) {
      return result::invalid_argument;
    }
    document_ptr source{yyjson_read(scene_json.data(), scene_json.size(), YYJSON_READ_NOFLAG)};
    auto* root = source ? yyjson_doc_get_root(source.get()) : nullptr;
    auto* format = yyjson_is_obj(root) ? yyjson_obj_get(root, "format") : nullptr;
    auto* version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : nullptr;
    auto* objects = yyjson_is_obj(root) ? yyjson_obj_get(root, "objects") : nullptr;
    auto* instances = yyjson_is_obj(root) ? yyjson_obj_get(root, "prefab_instances") : nullptr;
    if (!yyjson_is_str(format) ||
        std::string_view{yyjson_get_str(format), yyjson_get_len(format)} != "gneiss.scene" ||
        !yyjson_is_uint(version) || yyjson_get_uint(version) != 4U || !yyjson_is_arr(objects) ||
        !yyjson_is_arr(instances)) {
      return result::invalid_argument;
    }
    if (contains_uuid(objects, "uuid", request.instance_uuid) ||
        contains_uuid(instances, "instance_uuid", request.instance_uuid)) {
      return result::invalid_argument;
    }
    std::unordered_set<std::string> included;
    yyjson_val* source_root = nullptr;
    auto operation = collect_subtree(objects, request.root_uuid, included, source_root);
    if (operation != result::success || contains_nested_prefab(instances, included)) {
      return operation == result::success ? result::unsupported : operation;
    }
    std::string prefab_json;
    operation = make_prefab_document(source.get(), included, request, prefab_json);
    if (operation != result::success) {
      return operation;
    }
    std::string replacement_scene;
    operation =
        make_scene_document(source.get(), included, source_root, request, replacement_scene);
    if (operation != result::success) {
      return operation;
    }
    std::vector<author_document_change> changes;
    changes.reserve(2U);
    changes.push_back({.path = std::string(request.prefab_path),
                       .baseline = std::nullopt,
                       .replacement = std::move(prefab_json)});
    changes.push_back({.path = std::string(request.scene_path),
                       .baseline = std::string(scene_json),
                       .replacement = std::move(replacement_scene)});
    out_changes.swap(changes);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    out_changes.clear();
    return result::internal;
  }
}

result invert_author_document_changes(std::span<const author_document_change> changes,
                                      std::vector<author_document_change>& out_changes) noexcept {
  try {
    std::vector<author_document_change> inverted;
    inverted.reserve(changes.size());
    for (const auto& change : changes) {
      inverted.push_back(
          {.path = change.path, .baseline = change.replacement, .replacement = change.baseline});
    }
    out_changes.swap(inverted);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::editor
