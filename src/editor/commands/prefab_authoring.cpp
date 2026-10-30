// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/prefab_authoring.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"

#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/scene_codec.hpp"
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

[[nodiscard]] std::optional<gneiss::engine::guid> uuid_member(yyjson_val* object,
                                                              const char* name) {
  auto text = string_member(object, name);
  if (!text)
    return std::nullopt;
  auto id = gneiss::engine::guid::parse(*text);
  if (!id || !*id || std::ranges::any_of(*text, [](char c) { return c >= 'A' && c <= 'F'; }))
    return std::nullopt;
  return id;
}

[[nodiscard]] bool contains_uuid(yyjson_val* values, const char* member,
                                 gneiss::engine::guid target) noexcept {
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
                            gneiss::engine::guid value) {
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

[[nodiscard]] gneiss::engine::error write_document(yyjson_mut_doc* document, std::string& output) {
  std::size_t length = 0U;
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr text(yyjson_mut_write(document, YYJSON_WRITE_PRETTY, &length), &std::free);
  if (!text) {
    return gneiss::engine::error::out_of_memory;
  }
  output.assign(text.get(), length);
  return gneiss::engine::error::success;
}

[[nodiscard]] gneiss::engine::error
collect_subtree(yyjson_val* objects, gneiss::engine::guid root_uuid,
                std::unordered_set<gneiss::engine::guid, gneiss::engine::guid_hash>& included,
                yyjson_val*& root_object) {
  const auto count = yyjson_arr_size(objects);
  for (std::size_t index = 0U; index < count; ++index) {
    auto* object = yyjson_arr_get(objects, index);
    const auto uuid = uuid_member(object, "uuid");
    if (!uuid) {
      return gneiss::engine::error::invalid_argument;
    }
    if (*uuid == root_uuid) {
      if (root_object != nullptr) {
        return gneiss::engine::error::invalid_argument;
      }
      root_object = object;
      included.emplace(*uuid);
    }
  }
  if (root_object == nullptr) {
    return gneiss::engine::error::not_found;
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0U; index < count; ++index) {
      auto* object = yyjson_arr_get(objects, index);
      auto* parent = yyjson_obj_get(object, "parent");
      const auto uuid = uuid_member(object, "uuid");
      if (uuid && yyjson_is_str(parent) &&
          included.contains(uuid_member(object, "parent").value_or(gneiss::engine::guid{})) &&
          included.emplace(*uuid).second) {
        changed = true;
      }
    }
  }
  return gneiss::engine::error::success;
}

[[nodiscard]] bool contains_nested_prefab(
    yyjson_val* objects,
    const std::unordered_set<gneiss::engine::guid, gneiss::engine::guid_hash>& included) {
  std::size_t index{}, maximum{};
  yyjson_val* object{};
  yyjson_arr_foreach(objects, index, maximum, object) {
    const auto uuid = uuid_member(object, "uuid");
    if (uuid && included.contains(*uuid) && yyjson_obj_get(object, "prefab"))
      return true;
  }
  return false;
}

[[nodiscard]] gneiss::engine::error make_prefab_document(
    yyjson_doc* source,
    const std::unordered_set<gneiss::engine::guid, gneiss::engine::guid_hash>& included,
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
      return gneiss::engine::error::out_of_memory;
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
      !put_uuid(document.get(), prefab_envelope, "prefab_uuid", request.prefab_uuid) ||
      !put_uuid(document.get(), prefab_envelope, "root_uuid", request.root_uuid) ||
      !yyjson_mut_obj_add_val(document.get(), prefab_envelope, "objects", prefab_objects)) {
    return gneiss::engine::error::out_of_memory;
  }
  yyjson_mut_doc_set_root(document.get(), prefab_envelope);
  return write_document(document.get(), output);
}

[[nodiscard]] gneiss::engine::error make_scene_document(
    yyjson_doc* source,
    const std::unordered_set<gneiss::engine::guid, gneiss::engine::guid_hash>& included,
    yyjson_val* source_root, const create_prefab_author_request& request, std::string& output) {
  mutable_document_ptr document(yyjson_doc_mut_copy(source, nullptr), &yyjson_mut_doc_free);
  auto* root = document ? yyjson_mut_doc_get_root(document.get()) : nullptr;
  auto* objects = yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "objects") : nullptr;
  auto* instances = yyjson_mut_is_obj(root) ? yyjson_mut_obj_get(root, "objects") : nullptr;
  if (!yyjson_mut_is_arr(objects) || !yyjson_mut_is_arr(instances)) {
    return gneiss::engine::error::invalid_argument;
  }
  for (std::size_t index = yyjson_mut_arr_size(objects); index != 0U; --index) {
    auto* object = yyjson_mut_arr_get(objects, index - 1U);
    auto* uuid = yyjson_mut_obj_get(object, "uuid");
    if (yyjson_mut_is_str(uuid) &&
        included.contains(
            gneiss::engine::guid::parse({yyjson_mut_get_str(uuid), yyjson_mut_get_len(uuid)})
                .value_or(gneiss::engine::guid{}))) {
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
    return gneiss::engine::error::out_of_memory;
  }
  return write_document(document.get(), output);
}

// 作者操作和实例化使用同一类型化入口，不维护另一份 JSON 覆盖语义。
struct prefab_author_input {
  engine::scene_description scene;
  engine::prefab_description prefab;
  std::size_t target{};
};
engine::result<prefab_author_input> read_prefab_author_input(std::string_view scene_json,
                                                             std::string_view prefab_json,
                                                             engine::guid instance,
                                                             std::string_view uri,
                                                             engine::scene_diagnostic* diagnostic) {
  auto scene = engine::decode_scene(std::as_bytes(std::span{scene_json.data(), scene_json.size()}));
  auto prefab =
      engine::decode_prefab(std::as_bytes(std::span{prefab_json.data(), prefab_json.size()}));
  if (!scene || !prefab) {
    const auto& failure = !scene ? scene.error() : prefab.error();
    if (diagnostic)
      *diagnostic = {.error = failure.error, .path = failure.path, .message = failure.message};
    return failure.error;
  }
  auto target = std::ranges::find(scene->objects, instance, &engine::object_description::uuid);
  if (target == scene->objects.end() || !target->prefab || target->prefab->uri != uri)
    return engine::error::not_found;
  const auto index = static_cast<std::size_t>(target - scene->objects.begin());
  return prefab_author_input{std::move(*scene), std::move(*prefab), index};
}
engine::error project_prefab(const engine::prefab_description& source,
                             const engine::object_description& instance,
                             engine::prefab_description& output,
                             engine::scene_diagnostic* diagnostic) {
  auto status = engine::prepare_prefab_reference(source, *instance.prefab, output, diagnostic);
  if (status.failed() && diagnostic)
    diagnostic->path = "/instances/" + instance.uuid.string() + diagnostic->path;
  return status;
}
std::string json_text(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

gneiss::engine::error
prepare_create_prefab(std::string_view scene_json, const create_prefab_author_request& request,
                      std::vector<author_document_change>& out_changes) noexcept {
  out_changes.clear();
  if (scene_json.empty() || request.scene_path.empty() || request.prefab_path.empty() ||
      request.scene_path == request.prefab_path || !request.root_uuid || !request.prefab_uuid ||
      !request.instance_uuid ||
      gneiss::engine::validate_uri(request.prefab_uri) != gneiss::engine::error::success) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    if (request.prefab_uri != std::string{"asset://"} + std::string{request.prefab_path}) {
      return gneiss::engine::error::invalid_argument;
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
      return gneiss::engine::error::invalid_argument;
    }
    if (contains_uuid(objects, "uuid", request.instance_uuid) ||
        contains_uuid(instances, "uuid", request.instance_uuid)) {
      return gneiss::engine::error::invalid_argument;
    }
    std::unordered_set<gneiss::engine::guid, gneiss::engine::guid_hash> included;
    yyjson_val* source_root = nullptr;
    auto operation = collect_subtree(objects, request.root_uuid, included, source_root);
    if (operation != gneiss::engine::error::success ||
        contains_nested_prefab(instances, included)) {
      return operation == gneiss::engine::error::success ? gneiss::engine::error::unsupported
                                                         : operation;
    }
    std::string prefab_json;
    operation = make_prefab_document(source.get(), included, request, prefab_json);
    if (operation != gneiss::engine::error::success) {
      return operation;
    }
    std::string replacement_scene;
    operation =
        make_scene_document(source.get(), included, source_root, request, replacement_scene);
    if (operation != gneiss::engine::error::success) {
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
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    out_changes.clear();
    return gneiss::engine::error::internal;
  }
}

engine::error prepare_apply_prefab(std::string_view scene_json, std::string_view prefab_json,
                                   const apply_prefab_author_request& request,
                                   apply_prefab_author_plan& out_plan,
                                   engine::scene_diagnostic* diagnostic) noexcept try {
  using namespace engine;
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
  auto prefab_bytes = encode_prefab(candidate);
  auto scene_bytes = encode_scene(input->scene);
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
} catch (const std::bad_alloc&) {
  out_plan = {};
  return engine::error::out_of_memory;
} catch (...) {
  out_plan = {};
  return engine::error::internal;
}

engine::error prepare_unpack_prefab(std::string_view scene_json, std::string_view prefab_json,
                                    const unpack_prefab_author_request& request,
                                    std::vector<author_document_change>& out_changes,
                                    engine::scene_diagnostic* diagnostic) noexcept try {
  using namespace engine;
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
        std::ranges::find(effective.objects, entry.source_node_uuid, &object_description::uuid) ==
            effective.objects.end() ||
        std::ranges::find(input->scene.objects, entry.target_node_uuid,
                          &object_description::uuid) != input->scene.objects.end())
      return error::invalid_argument;
  }
  const auto root = mapping.at(effective.root_uuid);
  input->scene.objects.erase(input->scene.objects.begin() +
                             static_cast<std::ptrdiff_t>(input->target));
  for (auto& object : input->scene.objects)
    if (object.parent_uuid == instance.uuid)
      object.parent_uuid = root;
  for (auto& object : effective.objects) {
    const auto source_id = object.uuid;
    object.uuid = mapping.at(source_id);
    if (source_id == effective.root_uuid) {
      object.name = instance.name;
      object.parent_uuid = instance.parent_uuid;
      object.transform = instance.transform;
    } else {
      if (!object.parent_uuid)
        return error::invalid_argument;
      object.parent_uuid = mapping.at(*object.parent_uuid);
    }
    object.prefab.reset();
    input->scene.objects.push_back(std::move(object));
  }
  auto bytes = encode_scene(input->scene);
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
} catch (const std::bad_alloc&) {
  out_changes.clear();
  return engine::error::out_of_memory;
} catch (...) {
  out_changes.clear();
  return engine::error::internal;
}

engine::result<std::vector<prefab_override_entry>>
describe_prefab_overrides(const engine::prefab_reference_description& reference,
                          const engine::prefab_description& source) noexcept try {
  using namespace engine;
  std::vector<prefab_override_entry> entries;
  const auto label = [&](const prefab_property_override_key& key) {
    auto object =
        std::ranges::find(source.objects, key.source_node_uuid, &object_description::uuid);
    std::string text =
        object == source.objects.end() ? key.source_node_uuid.string() : object->name;
    auto type = meta::describe_type(key.type_guid);
    text += " / " + (type ? std::string(type->name) : key.type_guid.string());
    if (!key.property_id.is_valid())
      return text;
    auto property = meta::describe_property(key.type_guid, key.property_id);
    text +=
        " / " + (property ? std::string(property->name) : std::to_string(key.property_id.get()));
    auto current = property ? property->type : meta::type{};
    for (const auto& step : key.path) {
      if (auto* id = std::get_if<meta::property_id>(&step)) {
        auto fields = current.properties();
        if (fields) {
          auto found = std::ranges::find(*fields, *id, &meta::property_info::id);
          if (found != fields->end()) {
            text += "." + std::string(found->name);
            current = found->type;
            continue;
          }
        }
        text += "." + std::to_string(id->get());
        current = {};
      } else {
        text += "[" + std::to_string(std::get<std::size_t>(step)) + "]";
        auto element = current.element_type();
        current = element ? *element : meta::type{};
      }
    }
    return text;
  };
  for (const auto& change : reference.overrides)
    entries.push_back({.key = change.key, .label = label(change.key)});
  for (const auto& change : reference.component_overrides) {
    prefab_property_override_key key{.source_node_uuid = change.source_node_uuid,
                                     .type_guid = change.type_guid};
    entries.push_back(
        {.key = key, .component = true, .added = change.added.has_value(), .label = label(key)});
  }
  return entries;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::result<std::vector<author_document_change>>
prepare_restore_prefab(std::string_view scene_json, std::string_view scene_path,
                       engine::guid instance, const prefab_override_entry* entry) noexcept try {
  using namespace engine;
  if (scene_path.empty() || !instance)
    return error::invalid_argument;
  auto scene = decode_scene(std::as_bytes(std::span{scene_json.data(), scene_json.size()}));
  if (!scene)
    return scene.error().error;
  auto object = std::ranges::find(scene->objects, instance, &object_description::uuid);
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
      return item.source_node_uuid == entry->key.source_node_uuid &&
             item.type_guid == entry->key.type_guid;
    });
    if (!removed)
      return error::not_found;
  } else {
    auto removed = std::erase_if(reference.overrides,
                                 [&](const auto& item) { return item.key == entry->key; });
    if (!removed)
      return error::not_found;
  }
  auto encoded = encode_scene(*scene);
  if (!encoded)
    return encoded.error().error;
  return std::vector<author_document_change>{{.path = std::string(scene_path),
                                              .baseline = std::string(scene_json),
                                              .replacement = json_text(*encoded)}};
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

} // namespace gneiss::editor
