// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/commands/prefab_authoring.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>

#include <gneiss/engine/function/scene/prefab_property_override.hpp>
#include <gneiss/engine/resource/asset_uri.hpp>
#include <gneiss/engine/resource/scene/prefab_description.hpp>
#include <unordered_map>

#include <yyjson.h>

#include <algorithm>
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

[[nodiscard]] std::optional<std::string_view> string_member(yyjson_val* object,
                                                            const char* name) noexcept {
  auto* value = yyjson_obj_get(object, name);
  if (!yyjson_is_str(value)) {
    return std::nullopt;
  }
  return std::string_view{yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] std::optional<gneiss::guid> uuid_member(yyjson_val* object,
                                                              const char* name) {
  auto text = string_member(object, name);
  if (!text)
    return std::nullopt;
  auto id = gneiss::guid::parse(*text);
  if (!id || !*id || std::ranges::any_of(*text, [](char c) { return c >= 'A' && c <= 'F'; }))
    return std::nullopt;
  return id;
}

[[nodiscard]] bool contains_uuid(yyjson_val* values, const char* member,
                                 gneiss::guid target) noexcept {
  for (std::size_t index = 0U; index < yyjson_arr_size(values); ++index) {
    const auto uuid = uuid_member(yyjson_arr_get(values, index), member);
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

[[nodiscard]] bool put_uuid(yyjson_mut_doc* document, yyjson_mut_val* object, const char* name,
                            gneiss::guid value) {
  return put_string(document, object, name, value.string());
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

[[nodiscard]] gneiss::error write_document(yyjson_mut_doc* document, std::string& output) {
  std::size_t length = 0U;
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr text(yyjson_mut_write(document, YYJSON_WRITE_PRETTY, &length), &std::free);
  if (!text) {
    return gneiss::error::out_of_memory;
  }
  output.assign(text.get(), length);
  return gneiss::error::success;
}

[[nodiscard]] gneiss::error
collect_subtree(yyjson_val* objects, gneiss::guid root_uuid,
                std::unordered_set<gneiss::guid, gneiss::guid_hash>& included,
                yyjson_val*& root_object) {
  const auto count = yyjson_arr_size(objects);
  for (std::size_t index = 0U; index < count; ++index) {
    auto* object = yyjson_arr_get(objects, index);
    const auto uuid = uuid_member(object, "uuid");
    if (!uuid) {
      return gneiss::error::invalid_argument;
    }
    if (*uuid == root_uuid) {
      if (root_object != nullptr) {
        return gneiss::error::invalid_argument;
      }
      root_object = object;
      included.emplace(*uuid);
    }
  }
  if (root_object == nullptr) {
    return gneiss::error::not_found;
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0U; index < count; ++index) {
      auto* object = yyjson_arr_get(objects, index);
      auto* parent = yyjson_obj_get(object, "parent");
      const auto uuid = uuid_member(object, "uuid");
      if (uuid && yyjson_is_obj(parent) &&
          included.contains(uuid_member(parent, "object").value_or(gneiss::guid{})) &&
          included.emplace(*uuid).second) {
        changed = true;
      }
    }
  }
  return gneiss::error::success;
}

[[nodiscard]] bool contains_nested_prefab(
    yyjson_val* objects,
    const std::unordered_set<gneiss::guid, gneiss::guid_hash>& included) {
  std::size_t index{}, maximum{};
  yyjson_val* object{};
  yyjson_arr_foreach(objects, index, maximum, object) {
    const auto uuid = uuid_member(object, "uuid");
    auto* prefab = yyjson_obj_get(object, "prefab");
    if (uuid && included.contains(*uuid) && prefab && !yyjson_is_null(prefab))
      return true;
  }
  return false;
}

[[nodiscard]] gneiss::error make_prefab_document(
    yyjson_doc* source,
    const std::unordered_set<gneiss::guid, gneiss::guid_hash>& included,
    const create_prefab_author_request& request, std::string& output) {
  mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
  auto* prefab_envelope = document ? yyjson_mut_obj(document.get()) : nullptr;
  auto* prefab_objects = document ? yyjson_mut_arr(document.get()) : nullptr;
  auto* source_root = yyjson_doc_get_root(source);
  auto* source_objects = yyjson_obj_get(source_root, "objects");
  yyjson_mut_val* prefab_root = nullptr;
  for (std::size_t index = 0U; index < yyjson_arr_size(source_objects); ++index) {
    auto* source_object = yyjson_arr_get(source_objects, index);
    const auto uuid = uuid_member(source_object, "uuid");
    if (!uuid || !included.contains(*uuid)) {
      continue;
    }
    auto* object = yyjson_val_mut_copy(document.get(), source_object);
    if (object == nullptr || !yyjson_mut_arr_add_val(prefab_objects, object)) {
      return gneiss::error::out_of_memory;
    }
    if (*uuid == request.root_uuid) {
      prefab_root = object;
    }
  }
  auto* identity = make_identity_transform(document.get());
  auto* parent = yyjson_mut_obj(document.get());
  if (prefab_root == nullptr || identity == nullptr || parent == nullptr ||
      !yyjson_mut_obj_add_null(document.get(), parent, "object") ||
      !yyjson_mut_obj_add_null(document.get(), parent, "source") ||
      !yyjson_mut_obj_put(prefab_root, yyjson_mut_strcpy(document.get(), "parent"), parent) ||
      !yyjson_mut_obj_put(prefab_root, yyjson_mut_strcpy(document.get(), "transform"), identity) ||
      !put_string(document.get(), prefab_envelope, "format", "gneiss.prefab") ||
      !yyjson_mut_obj_put(prefab_envelope, yyjson_mut_strcpy(document.get(), "version"),
                          yyjson_mut_uint(document.get(), 1U)) ||
      !put_uuid(document.get(), prefab_envelope, "prefab_uuid", request.prefab_uuid) ||
      !put_uuid(document.get(), prefab_envelope, "root_uuid", request.root_uuid) ||
      !yyjson_mut_obj_add_val(document.get(), prefab_envelope, "objects", prefab_objects)) {
    return gneiss::error::out_of_memory;
  }
  yyjson_mut_doc_set_root(document.get(), prefab_envelope);
  return write_document(document.get(), output);
}

[[nodiscard]] gneiss::error make_scene_document(
    yyjson_doc* source,
    const std::unordered_set<gneiss::guid, gneiss::guid_hash>& included,
    yyjson_val* source_root, const create_prefab_author_request& request, std::string& output) {
  mutable_document_ptr document(yyjson_doc_mut_copy(source, nullptr), &yyjson_mut_doc_free);
  auto* root = document ? yyjson_mut_doc_get_root(document.get()) : nullptr;
  auto* objects = yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "objects") : nullptr;
  auto* instances = yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "objects") : nullptr;
  if (!yyjson_mut_is_arr(objects) || !yyjson_mut_is_arr(instances)) {
    return gneiss::error::invalid_argument;
  }
  for (std::size_t index = yyjson_mut_arr_size(objects); index != 0U; --index) {
    auto* object = yyjson_mut_arr_get(objects, index - 1U);
    auto* uuid = yyjson_mut_obj_get(object, "uuid");
    if (yyjson_mut_is_str(uuid) &&
        included.contains(
            gneiss::guid::parse({yyjson_mut_get_str(uuid), yyjson_mut_get_len(uuid)})
                .value_or(gneiss::guid{}))) {
      (void)yyjson_mut_arr_remove(objects, index - 1U);
    }
  }

  auto* instance = yyjson_mut_obj(document.get());
  auto* source_parent = yyjson_obj_get(source_root, "parent");
  auto* source_transform = yyjson_obj_get(source_root, "transform");
  const auto fallback_name = request.root_uuid.string();
  const auto name = string_member(source_root, "name").value_or(fallback_name);
  auto* parent = yyjson_val_mut_copy(document.get(), source_parent);
  auto* transform = yyjson_val_mut_copy(document.get(), source_transform);
  auto* overrides = yyjson_mut_arr(document.get());
  auto* reference = yyjson_mut_obj(document.get());
  if (instance == nullptr || parent == nullptr || transform == nullptr || overrides == nullptr ||
      !put_uuid(document.get(), instance, "uuid", request.instance_uuid) ||
      !put_string(document.get(), instance, "name", name) ||
      !yyjson_mut_obj_add_val(document.get(), instance, "parent", parent) ||
      !put_string(document.get(), reference, "uri", request.prefab_uri) ||
      !yyjson_mut_obj_add_val(document.get(), instance, "prefab", reference) ||
      !yyjson_mut_obj_add_val(document.get(), instance, "transform", transform) ||
      !yyjson_mut_obj_add_val(document.get(), reference, "overrides", overrides) ||
      !yyjson_mut_arr_add_val(instances, instance)) {
    return gneiss::error::out_of_memory;
  }
  return write_document(document.get(), output);
}

// 作者操作和实例化使用同一类型化入口，不维护另一份 JSON 覆盖语义。
struct prefab_author_input {
  gneiss::scene_description scene;
  gneiss::prefab_description prefab;
  std::size_t target{};
};
gneiss::result<prefab_author_input> read_prefab_author_input(std::string_view scene_json,
                                                             std::string_view prefab_json,
                                                             gneiss::guid instance,
                                                             std::string_view uri,
                                                             gneiss::scene_diagnostic* diagnostic) {
  auto scene = gneiss::asset_file::deserialize<gneiss::scene_description>(
      std::as_bytes(std::span{scene_json.data(), scene_json.size()}));
  auto prefab = gneiss::asset_file::deserialize<gneiss::prefab_description>(
      std::as_bytes(std::span{prefab_json.data(), prefab_json.size()}));
  if (!scene || !prefab) {
    const auto& failure = !scene ? scene.error() : prefab.error();
    if (diagnostic)
      *diagnostic = {.error = failure.error, .path = failure.path, .message = failure.message};
    return failure.error;
  }
  auto target = std::ranges::find(scene->objects, instance, &gneiss::game_object_description::uuid);
  if (target == scene->objects.end() || !target->prefab || target->prefab->uri != uri)
    return gneiss::error::not_found;
  const auto index = static_cast<std::size_t>(target - scene->objects.begin());
  return prefab_author_input{std::move(*scene), std::move(*prefab), index};
}
gneiss::error project_prefab(const gneiss::prefab_description& source,
                             const gneiss::game_object_description& instance,
                             gneiss::prefab_description& output,
                             gneiss::scene_diagnostic* diagnostic) {
  auto status = gneiss::prepare_prefab_reference(source, *instance.prefab, output, diagnostic);
  if (status.failed() && diagnostic)
    diagnostic->path = "/instances/" + instance.uuid.string() + diagnostic->path;
  return status;
}
std::string json_text(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

gneiss::error prepare_create_prefab(std::string_view scene_json,
                                            const create_prefab_author_request& request,
                                            std::vector<author_document_change>& out_changes) {
  out_changes.clear();
  if (scene_json.empty() || request.scene_path.empty() || request.prefab_path.empty() ||
      request.scene_path == request.prefab_path || !request.root_uuid || !request.prefab_uuid ||
      !request.instance_uuid ||
      gneiss::validate_uri(request.prefab_uri) != gneiss::error::success) {
    return gneiss::error::invalid_argument;
  }
  try {
    if (request.prefab_uri != std::string{"asset://"} + std::string{request.prefab_path}) {
      return gneiss::error::invalid_argument;
    }
    document_ptr source{yyjson_read(scene_json.data(), scene_json.size(), YYJSON_READ_NOFLAG)};
    auto* root = source ? yyjson_doc_get_root(source.get()) : nullptr;
    auto* format = yyjson_is_obj(root) ? yyjson_obj_get(root, "format") : nullptr;
    auto* version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : nullptr;
    auto* objects = yyjson_is_obj(root) ? yyjson_obj_get(root, "objects") : nullptr;
    auto* instances = yyjson_is_obj(root) ? yyjson_obj_get(root, "objects") : nullptr;
    if (!yyjson_is_str(format) ||
        std::string_view{yyjson_get_str(format), yyjson_get_len(format)} != "gneiss.scene" ||
        !yyjson_is_uint(version) || yyjson_get_uint(version) != 5U || !yyjson_is_arr(objects) ||
        !yyjson_is_arr(instances)) {
      return gneiss::error::invalid_argument;
    }
    if (contains_uuid(objects, "uuid", request.instance_uuid) ||
        contains_uuid(instances, "uuid", request.instance_uuid)) {
      return gneiss::error::invalid_argument;
    }
    std::unordered_set<gneiss::guid, gneiss::guid_hash> included;
    yyjson_val* source_root = nullptr;
    auto operation = collect_subtree(objects, request.root_uuid, included, source_root);
    if (operation != gneiss::error::success ||
        contains_nested_prefab(instances, included)) {
      return operation == gneiss::error::success ? gneiss::error::unsupported
                                                         : operation;
    }
    std::string prefab_json;
    operation = make_prefab_document(source.get(), included, request, prefab_json);
    if (operation != gneiss::error::success) {
      return operation;
    }
    std::string replacement_scene;
    operation =
        make_scene_document(source.get(), included, source_root, request, replacement_scene);
    if (operation != gneiss::error::success) {
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
    return gneiss::error::success;
  } catch (...) {
    // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
    out_changes.clear();
    throw;
  }
}

gneiss::error prepare_apply_prefab(std::string_view scene_json, std::string_view prefab_json,
                                   const apply_prefab_author_request& request,
                                   apply_prefab_author_plan& out_plan,
                                   gneiss::scene_diagnostic* diagnostic) try {
  using namespace gneiss;
  out_plan = {};
  if (diagnostic)
    *diagnostic = {};
  if (scene_json.empty() || prefab_json.empty() || request.scene_path.empty() ||
      request.prefab_path.empty() || request.scene_path == request.prefab_path ||
      !request.instance_uuid || validate_uri(request.prefab_uri).failed() ||
      request.prefab_uri != std::string{"asset://"} + std::string{request.prefab_path})
    return error::invalid_argument;
  auto input = read_prefab_author_input(scene_json, prefab_json, request.instance_uuid,
                                        request.prefab_uri, diagnostic);
  if (!input)
    return input.error();
  auto& target = input->scene.objects[input->target];
  if (target.prefab->overrides.empty() && target.prefab->component_overrides.empty())
    return error::not_ready;
  prefab_description candidate;
  auto status = project_prefab(input->prefab, target, candidate, diagnostic);
  if (status.failed())
    return status;
  target.prefab->overrides.clear();
  target.prefab->component_overrides.clear();
  apply_prefab_author_plan plan;
  // 当前实例清除已应用覆盖，其他同源实例保留自己的覆盖；全部通过后才生成写入计划。
  for (const auto& object : input->scene.objects) {
    if (!object.prefab || object.prefab->uri != request.prefab_uri)
      continue;
    prefab_description effective;
    status = project_prefab(candidate, object, effective, diagnostic);
    if (status.failed())
      return status;
    plan.affected_instance_uuids.push_back(object.uuid);
  }
  auto prefab_bytes = gneiss::asset_file::serialize_bytes(candidate);
  auto scene_bytes = gneiss::asset_file::serialize_bytes(input->scene);
  if (!prefab_bytes || !scene_bytes) {
    const auto& failure = !prefab_bytes ? prefab_bytes.error() : scene_bytes.error();
    if (diagnostic)
      *diagnostic = {.error = failure.error, .path = failure.path, .message = failure.message};
    return failure.error;
  }
  plan.changes.push_back({.path = std::string(request.prefab_path),
                          .baseline = std::string(prefab_json),
                          .replacement = json_text(*prefab_bytes)});
  plan.changes.push_back({.path = std::string(request.scene_path),
                          .baseline = std::string(scene_json),
                          .replacement = json_text(*scene_bytes)});
  out_plan = std::move(plan);
  return error::success;
} catch (...) {
  // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
  out_plan = {};
  throw;
}

gneiss::error prepare_unpack_prefab(std::string_view scene_json, std::string_view prefab_json,
                                    const unpack_prefab_author_request& request,
                                    std::vector<author_document_change>& out_changes,
                                    gneiss::scene_diagnostic* diagnostic) try {
  using namespace gneiss;
  out_changes.clear();
  if (diagnostic)
    *diagnostic = {};
  if (scene_json.empty() || prefab_json.empty() || request.scene_path.empty() ||
      !request.instance_uuid || request.node_mappings.empty() ||
      validate_uri(request.prefab_uri).failed())
    return error::invalid_argument;
  auto input = read_prefab_author_input(scene_json, prefab_json, request.instance_uuid,
                                        request.prefab_uri, diagnostic);
  if (!input)
    return input.error();
  const auto instance = input->scene.objects[input->target];
  prefab_description effective;
  auto status = project_prefab(input->prefab, instance, effective, diagnostic);
  if (status.failed())
    return status;
  if (request.node_mappings.size() != effective.objects.size())
    return error::invalid_argument;
  std::unordered_map<guid, guid, guid_hash> mapping;
  std::unordered_set<guid, guid_hash> targets;
  for (const auto& entry : request.node_mappings) {
    if (!entry.source_node_uuid || !entry.target_node_uuid ||
        !mapping.emplace(entry.source_node_uuid, entry.target_node_uuid).second ||
        !targets.insert(entry.target_node_uuid).second ||
        std::ranges::find(effective.objects, entry.source_node_uuid,
                          &game_object_description::uuid) == effective.objects.end() ||
        std::ranges::find(input->scene.objects, entry.target_node_uuid,
                          &game_object_description::uuid) != input->scene.objects.end())
      return error::invalid_argument;
  }
  input->scene.objects.erase(input->scene.objects.begin() +
                             static_cast<std::ptrdiff_t>(input->target));
  for (auto& object : input->scene.objects)
    if (object.parent.object == instance.uuid) {
      const auto source_parent = object.parent.source ? object.parent.source : effective.root_uuid;
      auto found = mapping.find(source_parent);
      if (found == mapping.end())
        return error::not_found;
      object.parent = {found->second, {}};
    }
  for (auto& object : effective.objects) {
    const auto source_id = object.uuid;
    object.uuid = mapping.at(source_id);
    if (source_id == effective.root_uuid) {
      object.name = instance.name;
      object.parent = instance.parent;
      object.transform = instance.transform;
    } else {
      if (!object.parent.object)
        return error::invalid_argument;
      object.parent.object = mapping.at(object.parent.object);
    }
    object.prefab.reset();
    input->scene.objects.push_back(std::move(object));
  }
  auto bytes = gneiss::asset_file::serialize_bytes(input->scene);
  if (!bytes) {
    if (diagnostic)
      *diagnostic = {.error = bytes.error().error,
                     .path = bytes.error().path,
                     .message = bytes.error().message};
    return bytes.error().error;
  }
  out_changes.push_back({.path = std::string(request.scene_path),
                         .baseline = std::string(scene_json),
                         .replacement = json_text(*bytes)});
  return error::success;
} catch (...) {
  // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
  out_changes.clear();
  throw;
}

gneiss::result<std::vector<prefab_override_entry>>
describe_prefab_overrides(const gneiss::prefab_reference_description& reference,
                          const gneiss::prefab_description& source) {
  using namespace gneiss;
  std::vector<prefab_override_entry> entries;
  const auto label = [&](const prefab_property_override_key& key) {
    auto object = std::ranges::find(source.objects, key.target.source_node_uuid,
                                    &game_object_description::uuid);
    std::string text =
        object == source.objects.end() ? key.target.source_node_uuid.string() : object->name;
    auto type = meta::find_type(key.target.target_type_guid);
    text += " / " + (type ? std::string(type.name()) : key.target.target_type_guid.string());
    if (!key.first_property().is_valid())
      return text;
    auto property = type ? type.find_property(key.first_property()) : meta::property{};
    text += " / " +
            (property ? std::string(property.name()) : std::to_string(key.first_property().get()));
    auto current = property ? property.type() : meta::type{};
    for (const auto& step : std::span{key.path}.subspan(1)) {
      if (auto* id = std::get_if<meta::property_id>(&step)) {
        auto fields = current.properties();
        if (!fields.empty()) {
          auto found = std::ranges::find(fields, *id, &meta::property::id);
          if (found != fields.end()) {
            text += "." + std::string(found->name());
            current = found->type();
            continue;
          }
        }
        text += "." + std::to_string(id->get());
        current = {};
      } else {
        text += "[" + meta::property_step_text(step) + "]";
        meta::type element;
        if (std::holds_alternative<std::string>(step)) {
          auto capability = current.capability<meta::associative_capability>();
          if (capability)
            element = capability.element_type();
        } else {
          auto capability = current.capability<meta::sequence_capability>();
          if (capability)
            element = capability.element_type();
        }
        current = element;
      }
    }
    return text;
  };
  for (const auto& change : reference.overrides)
    entries.push_back({.key = change.key, .label = label(change.key)});
  for (const auto& change : reference.component_overrides) {
    prefab_property_override_key key{.target = {.source_node_uuid = change.source_node_uuid,
                                                .target_type_guid = change.type_guid,
                                                .component_uuid = change.component_uuid},
                                     .path = {}};
    entries.push_back(
        {.key = key, .component = true, .added = change.added.has_value(), .label = label(key)});
  }
  return entries;
}

gneiss::result<std::vector<author_document_change>>
prepare_restore_prefab_impl(std::string_view scene_json, std::string_view scene_path,
                            gneiss::guid instance, const prefab_override_entry* entry,
                            bool whole_component) {
  using namespace gneiss;
  if (scene_path.empty() || !instance)
    return error::invalid_argument;
  auto scene = gneiss::asset_file::deserialize<scene_description>(
      std::as_bytes(std::span{scene_json.data(), scene_json.size()}));
  if (!scene)
    return scene.error().error;
  auto object = std::ranges::find(scene->objects, instance, &game_object_description::uuid);
  if (object == scene->objects.end() || !object->prefab)
    return error::not_found;
  auto& reference = *object->prefab;
  if (!entry) {
    if (reference.overrides.empty() && reference.component_overrides.empty())
      return error::not_ready;
    reference.overrides.clear();
    reference.component_overrides.clear();
  } else if (entry->component) {
    auto removed = std::erase_if(reference.component_overrides, [&](const auto& item) {
      return item.source_node_uuid == entry->key.target.source_node_uuid &&
             item.type_guid == entry->key.target.target_type_guid;
    });
    if (whole_component)
      removed += std::erase_if(reference.overrides, [&](const auto& item) {
        return item.key.target.source_node_uuid == entry->key.target.source_node_uuid &&
               component_description_type(item.key.target.target_type_guid) ==
                   entry->key.target.target_type_guid;
      });
    if (!removed)
      return error::not_found;
  } else {
    auto removed = std::erase_if(reference.overrides,
                                 [&](const auto& item) { return item.key == entry->key; });
    if (!removed)
      return error::not_found;
  }
  auto encoded = gneiss::asset_file::serialize_bytes(*scene);
  if (!encoded)
    return encoded.error().error;
  return std::vector<author_document_change>{{.path = std::string(scene_path),
                                              .baseline = std::string(scene_json),
                                              .replacement = json_text(*encoded)}};
}

gneiss::result<std::vector<author_document_change>>
prepare_restore_prefab(std::string_view json, std::string_view path, gneiss::guid instance,
                       const prefab_override_entry* entry) noexcept {
  return prepare_restore_prefab_impl(json, path, instance, entry, false);
}
gneiss::result<std::vector<author_document_change>>
prepare_restore_prefab_component(std::string_view json, std::string_view path,
                                 gneiss::guid instance, gneiss::guid object,
                                 gneiss::guid component) noexcept {
  if (!object || !component)
    return gneiss::error::invalid_argument;
  prefab_override_entry entry{
      .key = {.target = {.source_node_uuid = object, .target_type_guid = component}, .path = {}},
      .component = true,
      .label = {}};
  return prepare_restore_prefab_impl(json, path, instance, &entry, true);
}

} // namespace gneiss::editor
