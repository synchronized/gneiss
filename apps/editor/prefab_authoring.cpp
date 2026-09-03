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

constexpr std::string_view transform_type_id = "69644f20b2d24e488c7491f4f952ec2d";

struct transform_override final {
  std::string_view source_uuid;
  std::string_view field_name;
  yyjson_val* value = nullptr;
};

[[nodiscard]] std::optional<transform_override>
read_transform_override(yyjson_val* override_value) noexcept {
  const auto source_uuid = string_member(override_value, "source_node_uuid");
  const auto type_id = string_member(override_value, "type_id");
  auto* field_id = yyjson_obj_get(override_value, "field_id");
  auto* wrapped_value = yyjson_obj_get(override_value, "value");
  const auto kind = string_member(wrapped_value, "kind");
  auto* value = yyjson_obj_get(wrapped_value, "value");
  if (!source_uuid || !type_id || *type_id != transform_type_id || !yyjson_is_uint(field_id) ||
      !kind || !yyjson_is_arr(value)) {
    return std::nullopt;
  }
  std::string_view field_name;
  std::string_view expected_kind;
  std::size_t expected_size = 0U;
  switch (yyjson_get_uint(field_id)) {
  case 1U:
    field_name = "translation";
    expected_kind = "vec3";
    expected_size = 3U;
    break;
  case 2U:
    field_name = "rotation";
    expected_kind = "quaternion";
    expected_size = 4U;
    break;
  case 3U:
    field_name = "scale";
    expected_kind = "vec3";
    expected_size = 3U;
    break;
  default:
    return std::nullopt;
  }
  if (*kind != expected_kind || yyjson_arr_size(value) != expected_size) {
    return std::nullopt;
  }
  return transform_override{.source_uuid = *source_uuid, .field_name = field_name, .value = value};
}

[[nodiscard]] yyjson_mut_val* find_mutable_by_uuid(yyjson_mut_val* values, const char* member,
                                                   std::string_view uuid) noexcept {
  for (std::size_t index = 0U; index < yyjson_mut_arr_size(values); ++index) {
    auto* candidate = yyjson_mut_arr_get(values, index);
    auto* value = yyjson_mut_obj_get(candidate, member);
    if (yyjson_mut_is_str(value) &&
        std::string_view{yyjson_mut_get_str(value), yyjson_mut_get_len(value)} == uuid) {
      return candidate;
    }
  }
  return nullptr;
}

[[nodiscard]] result apply_overrides_to_prefab(yyjson_doc* prefab_source, yyjson_val* overrides,
                                               std::string& output) {
  mutable_document_ptr document(yyjson_doc_mut_copy(prefab_source, nullptr), &yyjson_mut_doc_free);
  auto* root = document ? yyjson_mut_doc_get_root(document.get()) : nullptr;
  auto* objects = yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "objects") : nullptr;
  if (!yyjson_mut_is_arr(objects)) {
    return result::invalid_argument;
  }
  for (std::size_t index = 0U; index < yyjson_arr_size(overrides); ++index) {
    const auto value = read_transform_override(yyjson_arr_get(overrides, index));
    if (!value) {
      return result::unsupported;
    }
    auto* object = find_mutable_by_uuid(objects, "uuid", value->source_uuid);
    auto* transform = object == nullptr ? nullptr : yyjson_mut_obj_get(object, "transform");
    auto* replacement = yyjson_val_mut_copy(document.get(), value->value);
    if (!yyjson_mut_is_obj(transform)) {
      return result::not_found;
    }
    if (replacement == nullptr ||
        !yyjson_mut_obj_put(
            transform,
            yyjson_mut_strncpy(document.get(), value->field_name.data(), value->field_name.size()),
            replacement)) {
      return result::out_of_memory;
    }
  }
  return write_document(document.get(), output);
}

[[nodiscard]] result clear_instance_overrides(yyjson_doc* scene_source,
                                              std::string_view instance_uuid, std::string& output) {
  mutable_document_ptr document(yyjson_doc_mut_copy(scene_source, nullptr), &yyjson_mut_doc_free);
  auto* root = document ? yyjson_mut_doc_get_root(document.get()) : nullptr;
  auto* instances =
      yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "prefab_instances") : nullptr;
  auto* instance = yyjson_mut_is_arr(instances)
                       ? find_mutable_by_uuid(instances, "instance_uuid", instance_uuid)
                       : nullptr;
  auto* empty = yyjson_mut_arr(document.get());
  if (instance == nullptr || empty == nullptr ||
      !yyjson_mut_obj_put(instance, yyjson_mut_strcpy(document.get(), "overrides"), empty)) {
    return instance == nullptr ? result::not_found : result::out_of_memory;
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

result prepare_apply_prefab(std::string_view scene_json, std::string_view prefab_json,
                            const apply_prefab_author_request& request,
                            apply_prefab_author_plan& out_plan) noexcept {
  out_plan = {};
  if (scene_json.empty() || prefab_json.empty() || request.scene_path.empty() ||
      request.prefab_path.empty() || request.scene_path == request.prefab_path ||
      !is_canonical_uuid(request.instance_uuid) ||
      gneiss_asset_uri_validate(request.prefab_uri.data(), request.prefab_uri.size()) !=
          GNEISS_SUCCESS) {
    return result::invalid_argument;
  }
  try {
    if (request.prefab_uri != std::string{"asset://"} + std::string{request.prefab_path}) {
      return result::invalid_argument;
    }
    document_ptr scene{yyjson_read(scene_json.data(), scene_json.size(), YYJSON_READ_NOFLAG)};
    document_ptr prefab{yyjson_read(prefab_json.data(), prefab_json.size(), YYJSON_READ_NOFLAG)};
    auto* scene_root = scene ? yyjson_doc_get_root(scene.get()) : nullptr;
    auto* prefab_root = prefab ? yyjson_doc_get_root(prefab.get()) : nullptr;
    auto* instances =
        yyjson_is_obj(scene_root) ? yyjson_obj_get(scene_root, "prefab_instances") : nullptr;
    auto* prefab_objects =
        yyjson_is_obj(prefab_root) ? yyjson_obj_get(prefab_root, "objects") : nullptr;
    if (!yyjson_is_arr(instances) || !yyjson_is_arr(prefab_objects)) {
      return result::invalid_argument;
    }
    yyjson_val* target = nullptr;
    std::vector<std::string> affected;
    for (std::size_t index = 0U; index < yyjson_arr_size(instances); ++index) {
      auto* instance = yyjson_arr_get(instances, index);
      const auto uuid = string_member(instance, "instance_uuid");
      const auto uri = string_member(instance, "prefab");
      if (uuid && uri && *uri == request.prefab_uri) {
        affected.emplace_back(*uuid);
        if (*uuid == request.instance_uuid) {
          target = instance;
        }
      }
    }
    auto* overrides = target == nullptr ? nullptr : yyjson_obj_get(target, "overrides");
    if (target == nullptr || !yyjson_is_arr(overrides)) {
      return result::not_found;
    }
    if (yyjson_arr_size(overrides) == 0U) {
      return result::not_ready;
    }
    std::string replacement_prefab;
    auto operation = apply_overrides_to_prefab(prefab.get(), overrides, replacement_prefab);
    if (operation != result::success) {
      return operation;
    }
    std::string replacement_scene;
    operation = clear_instance_overrides(scene.get(), request.instance_uuid, replacement_scene);
    if (operation != result::success) {
      return operation;
    }
    apply_prefab_author_plan plan;
    plan.changes.reserve(2U);
    plan.changes.push_back({.path = std::string(request.prefab_path),
                            .baseline = std::string(prefab_json),
                            .replacement = std::move(replacement_prefab)});
    plan.changes.push_back({.path = std::string(request.scene_path),
                            .baseline = std::string(scene_json),
                            .replacement = std::move(replacement_scene)});
    plan.affected_instance_uuids.swap(affected);
    out_plan = std::move(plan);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    out_plan = {};
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
