// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "core/serialization/detail/traversal.hpp"
#include <algorithm>
#include <gneiss/engine/core/meta/capabilities/container.hpp>
#include <gneiss/engine/core/meta/capabilities/enumeration.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/capabilities/representation.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/meta/resolve.hpp>
namespace gneiss::serialization::detail {
using namespace meta;
std::string field_path(std::string_view name) {
  std::string path = "/";
  for (char c : name) {
    if (c == '~')
      path += "~0";
    else if (c == '/')
      path += "~1";
    else
      path += c;
  }
  return path;
}
serialization::diagnostic failure(error code, std::string path = {}, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = code};
}
serialization::diagnostic prefix(serialization::diagnostic diagnostic, const std::string& path) {
  diagnostic.path = path + diagnostic.path;
  // 子 codec 的偏移相对于重编码片段，不冒充完整文档偏移。
  diagnostic.byte_offset = 0;
  return diagnostic;
}
/** 静态转换仅处理内存；路径、预算和候选校验仍由序列化持有。 */
result<writer::node, diagnostic>
encode_representation(const write_context& ctx, const const_instance& source, writer& output) {
  if (context_access::depth(ctx) >= ctx.maximum_depth())
    return failure(error::invalid_argument);
  result<writer::node, diagnostic> encoded = failure(error::unsupported);
  bool visited = false;
  auto status = source.type().capability<gneiss::meta::cap::representation>().encode(
      source, [&](const const_instance& representation) {
        visited = true;
        encoded = encode_view(ctx, source.registry(), representation, &output,
                              context_access::depth(ctx) + 1);
        return encoded ? error::success : encoded.error().error;
      });
  if (status.failed() && !visited)
    return failure(status);
  return encoded;
}
diagnostic decode_representation(const read_context& ctx, const instance& target,
                                 reader::node input) {
  if (context_access::depth(ctx) >= ctx.maximum_depth())
    return failure(error::invalid_argument);
  diagnostic diagnostic;
  auto status = target.type().capability<gneiss::meta::cap::representation>().decode(
      [&](const instance& representation) {
        diagnostic = decode_view(ctx, target.registry(), representation, input,
                                 context_access::depth(ctx) + 1);
        return diagnostic.error;
      },
      [&](const instance& candidate) {
        const auto policy = policies_of(candidate.type());
        if (policy.validation.validate && !(ctx.references() && ctx.references()->collecting())) {
          auto address = candidate.object_data();
          if (!address)
            return address.error();
          diagnostic = policy.validation.validate(*address);
          if (diagnostic.error.failed())
            return diagnostic.error;
        }
        if (candidate.type().kind() == type_kind::scalar) {
          auto value = candidate.read_value();
          return value ? target.assign(*value) : value.error();
        }
        return target.assign(candidate.as_const());
      });
  return diagnostic.error.failed() ? diagnostic : failure(status);
}
/** 统一验证反射对象实际身份，普通值结构无此要求。 */
serialization::diagnostic validate_identity(const registry_ref& registry, type_id id,
                                            const void* address) {
  auto type = resolve(registry, id);
  if (!type)
    return failure(error::not_found);
  auto object_type = meta::resolve<meta::object>(registry);
  if (!type.capability<meta::cap::object>().is_a(object_type))
    return {};
  auto instance = meta::const_instance::from_object(type, address);
  if (!instance)
    return failure(instance.error());
  auto cast = instance->capability<meta::cap::object>()->cast(object_type);
  if (!cast)
    return failure(cast.error());
  auto* object = static_cast<const meta::object*>(*cast->object_data());
  const auto actual = object ? object->dynamic_type(registry) : meta::type{};
  if (!type || !actual || actual.id() != id || actual.guid() != type.guid() ||
      object->dynamic_address() != address)
    return failure(error::invalid_argument, "", "对象实际身份与注册类型不符");
  return {};
}
/** 检查注册继承链并按 C++ 转换逐层调整地址。 */
result<void*, serialization::diagnostic> cast_base(const registry_ref& registry, type_id actual,
                                                   type_id declared, void* pointer) {
  auto actual_type = resolve(registry, actual);
  auto declared_type = declared == runtime_type_of<meta::object>()
                           ? meta::resolve<meta::object>(registry)
                           : resolve(registry, declared);
  auto capability = actual_type.capability<meta::cap::object>();
  if (!capability.is_a(declared_type))
    return failure(error::invalid_argument, "", "实际类型不属于声明基类");
  if (!pointer)
    return nullptr;
  auto root = instance::from_object(actual_type, pointer);
  if (!root)
    return failure(root.error());
  auto adjusted = root->capability<meta::cap::object>()->cast(declared_type);
  if (!adjusted)
    return failure(adjusted.error());
  auto address = adjusted->object_data();
  return address ? result<void*, serialization::diagnostic>{*address} : failure(address.error());
}

result<writer::node, serialization::diagnostic> encode_members(const write_context& ctx,
                                                               const registry_ref&, type_id,
                                                               const void*, writer*, std::size_t);
result<writer::node, serialization::diagnostic> encode_nested(const write_context& ctx,
                                                              const registry_ref& registry,
                                                              type_id id, const void* object,
                                                              writer* doc, std::size_t depth) {
  depth_scope scope{ctx, depth};
  if (depth > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  if (!object)
    return failure(error::internal);
  auto binding = find_policies(registry, id);
  if (!binding)
    return failure(binding.error());
  auto identity = validate_identity(registry, id, object);
  if (identity.error.failed())
    return identity;
  if (binding->validation.validate) {
    auto status = binding->validation.validate(object);
    if (status.error.failed())
      return status;
  }
  auto type = resolve(registry, id);
  if (type.capability<gneiss::meta::cap::representation>()) {
    auto view = const_instance::from_object(type, object);
    if (!view)
      return failure(view.error());
    return encode_representation(ctx, *view, *doc);
  }
  if (binding->content.encode) {
    auto encoded = binding->content.encode(ctx, *doc, object);
    if (!encoded)
      return std::move(encoded).error();
    if (!doc->is_object(*encoded))
      return failure(error::invalid_argument, "", "嵌套 codec 必须输出对象");
    return encoded;
  }
  return encode_members(ctx, registry, id, object, doc, depth);
}
result<writer::node, serialization::diagnostic>
encode_nonobject(const write_context&, const registry_ref&, const const_instance&, writer*,
                 std::size_t, const reference_policy*);
/** 对象递归使用轻量栈帧，不携带各容器分支的临时状态。 */
result<writer::node, serialization::diagnostic>
encode_view(const write_context& ctx, const registry_ref& registry, const const_instance& view,
            writer* doc, std::size_t depth, const reference_policy* override) {
  depth_scope scope{ctx, depth};
  if (view.type().capability<gneiss::meta::cap::representation>() && !view.type().registered()) {
    if (depth > ctx.maximum_depth())
      return failure(error::invalid_argument);
    return encode_representation(ctx, view, *doc);
  }
  if (view.type().kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return encode_nested(ctx, registry, view.type().id(), *p, doc, depth);
  }
  if (view.type().capability<gneiss::meta::cap::representation>()) {
    if (depth > ctx.maximum_depth())
      return failure(error::invalid_argument);
    return encode_representation(ctx, view, *doc);
  }
  return encode_nonobject(ctx, registry, view, doc, depth, override);
}
result<writer::node, serialization::diagnostic>
encode_nonobject(const write_context& ctx, const registry_ref& registry, const const_instance& view,
                 writer* doc, std::size_t depth, const reference_policy* override) {
  depth_scope scope{ctx, depth};
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::empty)
    return doc->null();
  if (value_type.kind() == type_kind::reference) {
    auto reference_slot = view.as_reference();
    if (!reference_slot)
      return failure(reference_slot.error());
    auto owned = reference_slot->owner();
    if (!owned)
      return doc->null();
    auto target = value_type.capability<gneiss::meta::cap::reference>().element_type();
    if (!target)
      return failure(error::unsupported);
    if (target.kind() != type_kind::object)
      return failure(error::unsupported, "", "内嵌引用目标必须是注册对象");
    auto declared = reference_policy_for(value_type, override);
    auto actual = reference_slot->actual_type().id();
    if (!declared)
      return failure(error::invalid_argument);
    if (declared->encode) {
      auto pointer = cast_base(registry, actual, target.id(), const_cast<void*>(owned.get()));
      if (!pointer)
        return std::move(pointer).error();
      return declared->encode(ctx, *doc, *pointer);
    }
    if (!declared->polymorphic && actual != target.id())
      return failure(error::invalid_argument, "", "未声明多态的引用不能保存派生对象");
    auto adjusted = cast_base(registry, actual, target.id(), nullptr);
    if (!adjusted)
      return std::move(adjusted).error();
    auto binding = find_policies(registry, actual);
    if (!binding)
      return failure(error::unsupported, "", "引用目标缺少编码器");
    const auto* address = owned.get();
    if (depth > ctx.maximum_depth() || !context_access::active(ctx).insert(address).second)
      return failure(error::invalid_argument, "", "引用环或嵌套深度超限");
    active_reference reference{ctx, address};
    auto content = encode_nested(ctx, registry, actual, address, doc, depth + 1);

    if (!content)
      return std::move(content).error();
    if (!declared->polymorphic) {
      if (doc->is_null(*content))
        return failure(error::invalid_argument, "", "非空内嵌引用不能编码为 null");
      return content;
    }
    auto actual_type = reference_slot->actual_type();
    if (!actual_type)
      return failure(error::not_found);
    if (!actual_type.guid())
      return failure(error::unsupported, "", "多态持久化需要稳定类型 GUID");
    auto* entry = doc->object();
    const auto guid = actual_type.guid().string();
    if (!entry || !doc->member(entry, "$type_guid", doc->string(guid)) ||
        !doc->member(entry, "$content", *content))
      return failure(error::out_of_memory);
    return entry;
  }
  if (depth > ctx.maximum_depth() && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? doc->scalar(*v)
             : result<writer::node, serialization::diagnostic>{failure(v.error())};
  }
  if (value_type.kind() == type_kind::variant) {
    auto variant = view.as_variant();
    if (!variant)
      return failure(variant.error());
    if (variant->names().empty())
      return failure(error::unsupported, "", "variant 未配置文件标签");
    auto index = variant->index();
    if (!index)
      return failure(index.error());
    auto child = variant->value();
    if (!child)
      return failure(child.error());
    auto content = encode_view(ctx, registry, *child, doc, depth + 1);
    if (!content)
      return prefix(std::move(content).error(), "/$content");
    auto* output = doc->object();
    auto name = variant->names()[*index];
    if (!output || !doc->member(output, "$kind", doc->string(name)) ||
        !doc->member(output, "$content", *content))
      return failure(error::out_of_memory);
    return output;
  }
  if (value_type.kind() == type_kind::optional) {
    auto v = view.as_optional();
    if (!v)
      return failure(v.error());
    auto has = v->has_value();
    if (!has) {
      auto* output = doc->null();
      return output ? result<writer::node, serialization::diagnostic>{output}
                    : failure(error::out_of_memory);
    }
    auto child = v->get();
    if (!child)
      return failure(child.error());
    auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
    if (encoded && doc->is_null(*encoded))
      return failure(error::unsupported, "", "optional 的非空值不能编码为 null");
    return encoded;
  }
  if (value_type.kind() == type_kind::sequence) {
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    auto count = v->size();
    auto* output = doc->array();
    if (!output)
      return failure(error::out_of_memory);
    for (std::size_t i = 0; i < count; ++i) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
      if (!encoded)
        return prefix(std::move(encoded).error(), "/" + std::to_string(i));
      if (!doc->append(output, *encoded))
        return failure(error::out_of_memory);
    }
    return output;
  }
  if (value_type.kind() == type_kind::associative) {
    auto v = view.as_associative();
    if (!v)
      return failure(v.error());
    auto keys = v->keys();
    if (!keys)
      return failure(keys.error());
    auto* output = doc->object();
    if (!output)
      return failure(error::out_of_memory);
    for (const auto& key : *keys) {
      auto child = v->find(key);
      if (!child)
        return failure(child.error());
      auto encoded = encode_view(ctx, registry, *child, doc, depth + 1);
      if (!encoded)
        return prefix(std::move(encoded).error(), field_path(key));
      if (!doc->member(output, key, *encoded))
        return failure(error::out_of_memory);
    }
    return output;
  }
  return failure(error::unsupported);
}
result<writer::node, serialization::diagnostic> encode_enum_name(const const_instance& view,
                                                                 writer* doc) {
  const auto type = view.type();
  const auto entries = type.capability<gneiss::meta::cap::enumeration>().entries();
  if (!type.is_enum() || !type.registered())
    return failure(error::invalid_argument, "", "属性必须是已注册枚举");
  auto scalar = view.read_value();
  if (!scalar)
    return failure(scalar.error());
  const auto found = std::ranges::find(entries, *scalar, &enum_entry::data);
  if (found == entries.end())
    return failure(error::invalid_argument, "", "未知枚举值");
  auto* name = doc->string(found->name);
  if (!name)
    return failure(error::out_of_memory);
  return name;
}
serialization::diagnostic decode_enum_name(const read_context& ctx, const instance& view,
                                           reader::node input) {
  const auto type = view.type();
  const auto entries = type.capability<gneiss::meta::cap::enumeration>().entries();
  if (!type.is_enum() || !type.registered() || !context_access::input(ctx)->is_string(input))
    return failure(error::invalid_argument, "", "属性必须是枚举名称");
  const auto name = context_access::input(ctx)->string(input);
  const auto found = std::ranges::find(entries, name, &enum_entry::name);
  if (found == entries.end())
    return failure(error::invalid_argument, "", "未知枚举名称");
  auto status = view.assign(found->data);
  return status.failed() ? failure(status) : serialization::diagnostic{};
}
result<writer::node, serialization::diagnostic> encode_members(const write_context& ctx,
                                                               const registry_ref& registry,
                                                               type_id id, const void* object,
                                                               writer* doc, std::size_t depth) {
  depth_scope scope{ctx, depth};
  if (depth > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  auto view = const_instance::from_object(gneiss::meta::resolve(registry, id), object);
  if (!view)
    return failure(view.error());
  auto properties = view->type().template capability<gneiss::meta::cap::object>().properties();
  if (!view->type().registered())
    return failure(error::not_found);
  auto* root = doc->object();
  if (!root)
    return failure(error::out_of_memory);
  for (const auto& property : properties) {
    auto policy = policy_of(property);
    if (!policy)
      return failure(policy.error(), field_path(property.name()), "字段序列化策略与访问能力不兼容");
    if (!policy->save ||
        !gneiss::meta::has_flags(property.capabilities(),
                                         gneiss::meta::property_capabilities::writable))
      continue;
    const auto path = field_path(property.name());
    auto child = property.get(*view);
    if (!child)
      return failure(child.error(), path);
    auto encoded = policy->enum_name
                       ? encode_enum_name(*child, doc)
                       : encode_view(ctx, registry, *child, doc, depth + 1,
                                     policy->reference ? &*policy->reference : nullptr);
    if (!encoded)
      return prefix(std::move(encoded).error(), path);
    if (!doc->member(root, property.name(), *encoded))
      return failure(error::out_of_memory);
  }
  return root;
}
serialization::diagnostic validate_defaults(const read_context& ctx, const registry_ref&, type_id,
                                            const void*, std::size_t);
serialization::diagnostic validate_view(const read_context& ctx, const registry_ref& registry,
                                        const const_instance& view, std::size_t depth) {
  depth_scope scope{ctx, depth};
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::empty)
    return {};
  if (value_type.kind() == type_kind::reference) {
    auto reference_slot = view.as_reference();
    if (!reference_slot)
      return failure(reference_slot.error());
    auto owner = reference_slot->owner();
    return !owner ? serialization::diagnostic{}
                  : failure(error::unsupported, "", "非空缺省引用必须显式保存");
  }
  if (depth > ctx.maximum_depth() && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.capability<gneiss::meta::cap::representation>()) {
    if (depth >= ctx.maximum_depth())
      return failure(error::invalid_argument, "", "类型表示超过深度限制");
    const auto policy = policies_of(value_type);
    if (policy.validation.validate && !(ctx.references() && ctx.references()->collecting())) {
      auto address = view.object_data();
      if (!address)
        return failure(address.error());
      auto status = policy.validation.validate(*address);
      if (status.error.failed())
        return status;
    }
    diagnostic checked;
    auto status = view.type().capability<gneiss::meta::cap::representation>().encode(
        view, [&](const const_instance& representation) {
          checked = validate_view(ctx, registry, representation, depth + 1);
          return checked.error;
        });
    return checked.error.failed() ? checked : failure(status);
  }
  if (value_type.kind() == type_kind::object) {
    auto p = view.object_data();
    if (!p)
      return failure(p.error());
    return validate_defaults(ctx, registry, value_type.id(), *p, depth);
  }
  if (value_type.kind() == type_kind::scalar) {
    auto v = view.read_value();
    return v ? serialization::diagnostic{} : failure(v.error());
  }
  if (value_type.kind() == type_kind::variant) {
    auto variant = view.as_variant();
    if (!variant)
      return failure(variant.error());
    auto child = variant->value();
    if (!child)
      return failure(child.error());
    auto status = validate_view(ctx, registry, *child, depth + 1);
    return status.error.failed() ? prefix(std::move(status), "/$content") : status;
  }
  if (value_type.kind() == type_kind::optional) {
    auto v = view.as_optional();
    if (!v)
      return failure(v.error());
    auto has = v->has_value();
    if (!has)
      return {};
    auto child = v->get();
    if (!child)
      return failure(child.error());
    return validate_view(ctx, registry, *child, depth + 1);
  }
  if (value_type.kind() == type_kind::sequence) {
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    auto n = v->size();
    for (std::size_t i = 0; i < n; ++i) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      auto status = validate_view(ctx, registry, *child, depth + 1);
      if (status.error.failed())
        return prefix(std::move(status), "/" + std::to_string(i));
    }
    return {};
  }
  auto v = view.as_associative();
  if (!v)
    return failure(v.error());
  auto keys = v->keys();
  if (!keys)
    return failure(keys.error());
  for (const auto& key : *keys) {
    auto child = v->find(key);
    if (!child)
      return failure(child.error());
    auto status = validate_view(ctx, registry, *child, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), field_path(key));
  }
  return {};
}
serialization::diagnostic validate_defaults(const read_context& ctx, const registry_ref& registry,
                                            type_id id, const void* object, std::size_t depth) {
  depth_scope scope{ctx, depth};
  if (depth > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  auto binding = find_policies(registry, id);
  if (!binding)
    return failure(binding.error());
  if (binding->validation.validate && !(ctx.references() && ctx.references()->collecting())) {
    auto status = binding->validation.validate(object);
    if (status.error.failed())
      return status;
  }
  auto view = const_instance::from_object(gneiss::meta::resolve(registry, id), object);
  if (!view)
    return failure(view.error());
  auto properties = view->type().template capability<gneiss::meta::cap::object>().properties();
  if (!view->type().registered())
    return failure(error::not_found);
  for (const auto& property : properties) {
    auto policy = policy_of(property);
    if (!policy)
      return failure(policy.error(), field_path(property.name()));
    if (!policy->save || property.type().kind() == type_kind::scalar ||
        !gneiss::meta::has_flags(property.capabilities(),
                                         gneiss::meta::property_capabilities::writable))
      continue;
    auto child = property.get(*view);
    if (!child)
      return failure(child.error(), field_path(property.name()));
    auto status = validate_view(ctx, registry, *child, depth + 1);
    if (status.error.failed())
      return prefix(std::move(status), field_path(property.name()));
  }
  return {};
}
serialization::diagnostic decode_members(const read_context& ctx, const registry_ref&, type_id,
                                         void*, reader::node, std::size_t,
                                         std::span<const std::string_view>);
result<ref<void>, serialization::diagnostic> decode_owned(const read_context& ctx,
                                                          const registry_ref& registry,
                                                          type_id actual, reader::node content,
                                                          std::size_t depth, bool root) {
  depth_scope scope{ctx, depth};
  if (depth > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "引用嵌套深度超限");
  auto binding = find_policies(registry, actual);
  if (!binding)
    return failure(binding.error());
  ref<void> object;
  serialization::diagnostic status;
  if (binding->content.decode &&
      !resolve(registry, actual).capability<gneiss::meta::cap::representation>()) {
    depth_scope custom_depth{ctx, depth + (root ? 0 : 1)};
    if (context_access::depth(ctx) > ctx.maximum_depth())
      return failure(error::invalid_argument);
    auto decoded = binding->content.decode(ctx, *context_access::input(ctx), content);
    if (!decoded)
      status = std::move(decoded).error();
    else
      object = std::move(*decoded);
  } else {
    auto capability = resolve(registry, actual).capability<meta::cap::object>();
    if (!capability.can_construct())
      return failure(error::unsupported);
    auto created = capability.construct();
    if (!created)
      return failure(created.error());
    object = std::move(*created);
    if (resolve(registry, actual).capability<gneiss::meta::cap::representation>()) {
      auto view = instance::from_object(gneiss::meta::resolve(registry, actual), object.get());
      if (!view)
        return failure(view.error());
      status = decode_representation(ctx, *view, content);
    } else {
      status = decode_members(ctx, registry, actual, object.get(), content, depth + (root ? 0 : 1));
    }
  }
  if (status.error.ok() && !object)
    status = failure(error::internal);
  if (status.error.ok())
    status = validate_identity(registry, actual, object.get());
  if (status.error.ok() &&
      !resolve(registry, actual).capability<gneiss::meta::cap::representation>() &&
      binding->validation.validate && !(ctx.references() && ctx.references()->collecting()))
    status = binding->validation.validate(object.get());
  if (status.error.failed())
    return status;
  return object;
}
serialization::diagnostic decode_nested(const read_context& ctx, const registry_ref& registry,
                                        const instance& target, reader::node input,
                                        std::size_t depth) {
  depth_scope scope{ctx, depth};
  if (depth > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  if (!context_access::input(ctx)->is_object(input))
    return failure(error::invalid_argument, "", "嵌套值必须是对象");
  auto binding = find_policies(registry, target.type().id());
  if (!binding)
    return failure(binding.error());
  if (binding->content.decode) {
    auto decoded = binding->content.decode(ctx, *context_access::input(ctx), input);
    if (!decoded)
      return std::move(decoded).error();
    if (!*decoded)
      return failure(error::internal);
    if (binding->validation.validate && !(ctx.references() && ctx.references()->collecting())) {
      auto status = binding->validation.validate(decoded->get());
      if (status.error.failed())
        return status;
    }
    return failure(target.assign_object(target.type().id(), decoded->get()));
  }
  auto address = target.object_data();
  if (!address)
    return failure(address.error());
  auto* object = *address;
  auto status = decode_members(ctx, registry, target.type().id(), object, input, depth);
  if (status.error.failed())
    return status;
  return binding->validation.validate && !(ctx.references() && ctx.references()->collecting())
             ? binding->validation.validate(object)
             : serialization::diagnostic{};
}
serialization::diagnostic decode_nonobject(const read_context&, const registry_ref&,
                                           const instance&, reader::node, std::size_t,
                                           const reference_policy*);
serialization::diagnostic decode_view(const read_context& ctx, const registry_ref& registry,
                                      const instance& view, reader::node input, std::size_t depth,
                                      const reference_policy* override) {
  depth_scope scope{ctx, depth};
  if (view.type().capability<gneiss::meta::cap::representation>()) {
    if (depth > ctx.maximum_depth())
      return failure(error::invalid_argument);
    return decode_representation(ctx, view, input);
  }
  if (view.type().kind() == type_kind::object && !view.requires_replacement())
    return decode_nested(ctx, registry, view, input, depth);
  return decode_nonobject(ctx, registry, view, input, depth, override);
}
serialization::diagnostic decode_nonobject(const read_context& ctx, const registry_ref& registry,
                                           const instance& view, reader::node input,
                                           std::size_t depth, const reference_policy* override) {
  depth_scope scope{ctx, depth};
  if (view.requires_replacement()) {
    serialization::diagnostic diagnostic;
    auto status = view.replace([&](const instance& temporary) {
      diagnostic = decode_view(ctx, registry, temporary, input, depth, override);
      return diagnostic.error;
    });
    return diagnostic.error.failed()
               ? diagnostic
               : (status.failed() ? failure(status) : serialization::diagnostic{});
  }
  const auto value_type = view.type();
  if (value_type.kind() == type_kind::empty)
    return context_access::input(ctx)->is_null(input) ? serialization::diagnostic{}
                                                      : failure(error::invalid_argument);
  if (value_type.kind() == type_kind::reference) {
    auto reference_slot = view.as_reference();
    if (!reference_slot)
      return failure(reference_slot.error());
    if (context_access::input(ctx)->is_null(input)) {
      reference_slot->reset();
      return {};
    }
    auto target = value_type.capability<gneiss::meta::cap::reference>().element_type();
    if (!target || target.kind() != type_kind::object)
      return failure(error::unsupported);
    auto declared = reference_policy_for(value_type, override);
    if (!declared)
      return failure(declared.error());
    if (declared->decode) {
      auto resolved = declared->decode(ctx, *context_access::input(ctx), input);
      if (!resolved)
        return std::move(resolved).error();
      if (!*resolved) {
        if (ctx.references() && ctx.references()->collecting()) {
          reference_slot->reset();
          return {};
        }
        return failure(error::invalid_argument, "", "非空引用解析结果为空");
      }
      return failure(reference_slot->assign_owner(target.id(), *resolved));
    }
    auto actual = target.id();
    auto* content = input;
    if (declared->polymorphic) {
      auto* tag = context_access::input(ctx)->member(input, "$type_guid");
      content = context_access::input(ctx)->member(input, "$content");
      if (!context_access::input(ctx)->is_object(input) ||
          context_access::input(ctx)->size(input) != 2 ||
          !context_access::input(ctx)->is_string(tag) || !content)
        return failure(error::invalid_argument, "", "多态引用需要类型和内容");
      auto guid = gneiss::guid::parse(context_access::input(ctx)->string(tag));
      if (!guid || !*guid || guid->string() != context_access::input(ctx)->string(tag))
        return failure(error::invalid_argument, "/$type_guid");
      auto type = resolve(registry, *guid);
      if (!type)
        return failure(gneiss::error{gneiss::error::not_found}, "/$type_guid");
      actual = type.id();
      if (!context_access::input(ctx)->is_object(content) &&
          !resolve(registry, actual).capability<gneiss::meta::cap::representation>())
        return failure(error::invalid_argument, "/$content", "对象内容需要对象或静态表示能力");
    }
    auto compatible = cast_base(registry, actual, target.id(), nullptr);
    if (!compatible)
      return std::move(compatible).error();
    path_scope reference_path{ctx, declared->polymorphic ? "/$content" : ""};
    auto decoded = decode_owned(ctx, registry, actual, content, depth);
    if (!decoded)
      return declared->polymorphic ? prefix(std::move(decoded).error(), "/$content")
                                   : std::move(decoded).error();
    auto object = std::move(*decoded);
    auto adjusted = cast_base(registry, actual, target.id(), object.get());
    if (!adjusted)
      return std::move(adjusted).error();
    auto assigned = reference_slot->assign_owner(target.id(), object.alias(*adjusted));
    return assigned.failed() ? failure(assigned) : serialization::diagnostic{};
  }
  if (depth > ctx.maximum_depth() && value_type.kind() != type_kind::scalar)
    return failure(error::invalid_argument, "", "嵌套值超过深度限制");
  if (value_type.kind() == type_kind::scalar) {
    auto decoded = context_access::input(ctx)->scalar(input, value_type.scalar_kind());
    if (!decoded)
      return std::move(decoded).error();
    auto status = view.assign(*decoded);
    return status.failed() ? failure(status) : serialization::diagnostic{};
  }
  if (value_type.kind() == type_kind::variant) {
    auto variant = view.as_variant();
    if (!variant)
      return failure(variant.error());
    if (variant->names().empty())
      return failure(error::unsupported, "", "variant 未配置文件标签");
    auto* tag = context_access::input(ctx)->is_object(input)
                    ? context_access::input(ctx)->member(input, "$kind")
                    : nullptr;
    auto* content = context_access::input(ctx)->is_object(input)
                        ? context_access::input(ctx)->member(input, "$content")
                        : nullptr;
    if (!context_access::input(ctx)->is_object(input) ||
        context_access::input(ctx)->size(input) != 2 ||
        !context_access::input(ctx)->is_string(tag) || !content)
      return failure(error::invalid_argument, "", "variant 必须包含 $kind 和 $content");
    const auto name = context_access::input(ctx)->string(tag);
    auto names = variant->names();
    auto found = std::ranges::find(names, name);
    if (found == names.end())
      return failure(error::invalid_argument, "/$kind", "未知 variant 标签");
    auto status = variant->select(static_cast<std::size_t>(found - names.begin()));
    if (status.failed())
      return failure(status, "/$kind");
    auto child = variant->value();
    if (!child)
      return failure(child.error());
    path_scope variant_path{ctx, "/$content"};
    auto decoded = decode_view(ctx, registry, *child, content, depth + 1);
    return decoded.error.failed() ? prefix(std::move(decoded), "/$content") : decoded;
  }
  if (value_type.kind() == type_kind::optional) {
    auto v = view.as_optional();
    if (!v)
      return failure(v.error());
    auto status = v->reset();
    if (status.failed())
      return failure(status);
    if (context_access::input(ctx)->is_null(input))
      return {};
    auto child = v->ensure();
    if (!child)
      return failure(child.error());
    return decode_view(ctx, registry, *child, input, depth + 1);
  }
  if (value_type.kind() == type_kind::sequence) {
    if (!context_access::input(ctx)->is_array(input))
      return failure(error::invalid_argument, "", "序列必须是数组");
    auto v = view.as_sequence();
    if (!v)
      return failure(v.error());
    if (v->supports_replacement()) {
      serialization::diagnostic decoded;
      auto status = v->replace([&](const instance& candidate) {
        decoded = decode_view(ctx, registry, candidate, input, depth);
        return decoded.error;
      });
      return decoded.error.failed() ? decoded : failure(status);
    }
    if (v->is_resizable()) {
      auto status = v->resize(0);
      if (status.failed())
        return failure(status);
    }
    auto status = v->resize(context_access::input(ctx)->size(input));
    if (status.failed())
      return failure(status);
    std::size_t i = 0;
    auto it = context_access::input(ctx)->iterate(input);
    while (auto* item = context_access::input(ctx)->next_element(it)) {
      auto child = v->at(i);
      if (!child)
        return failure(child.error());
      path_scope element_path{ctx, "/" + std::to_string(i)};
      auto decoded = decode_view(ctx, registry, *child, item, depth + 1);
      if (decoded.error.failed())
        return prefix(std::move(decoded), "/" + std::to_string(i));
      ++i;
    }
    return {};
  }
  if (value_type.kind() == type_kind::associative) {
    if (!context_access::input(ctx)->is_object(input))
      return failure(error::invalid_argument, "", "关联容器必须是对象");
    auto v = view.as_associative();
    if (!v)
      return failure(v.error());
    auto status = v->clear();
    if (status.failed())
      return failure(status);
    std::unordered_set<std::string_view> keys;
    auto it = context_access::input(ctx)->iterate(input);
    while (auto entry = context_access::input(ctx)->next_member(it)) {
      const auto name = entry.key;
      if (!keys.insert(name).second)
        return failure(error::invalid_argument, field_path(name), "重复关联键");
      auto child = v->insert(name);
      if (!child)
        return failure(child.error(), field_path(name));
      path_scope element_path{ctx, field_path(name)};
      auto decoded = decode_view(ctx, registry, *child, entry.data, depth + 1);
      if (decoded.error.failed())
        return prefix(std::move(decoded), field_path(name));
    }
    return {};
  }
  return failure(error::unsupported);
}
serialization::diagnostic decode_members(const read_context& ctx, const registry_ref& registry,
                                         type_id id, void* object, reader::node root,
                                         std::size_t depth,
                                         std::span<const std::string_view> ignored) {
  depth_scope scope{ctx, depth};
  if (depth > ctx.maximum_depth())
    return failure(error::invalid_argument, "", "嵌套对象超过深度限制");
  auto view = instance::from_object(gneiss::meta::resolve(registry, id), object);
  if (!view)
    return failure(view.error());
  auto properties =
      view->as_const().type().template capability<gneiss::meta::cap::object>().properties();
  if (!view->type().registered())
    return failure(error::not_found);
  if (!context_access::input(ctx)->is_object(root))
    return failure(error::invalid_argument, "", "根值必须是对象");
  auto codec = find_policies(registry, id);
  if (!codec)
    return failure(codec.error());
  std::unordered_set<std::string_view> names;
  auto it = context_access::input(ctx)->iterate(root);
  while (auto entry = context_access::input(ctx)->next_member(it)) {
    const auto name = entry.key;
    if (std::ranges::find(ignored, name) != ignored.end())
      continue;
    if (codec->object.reject_unknown_fields &&
        std::ranges::none_of(properties, [&](const auto& p) { return p.name() == name; }))
      return failure(error::invalid_argument, field_path(name), "未知字段");
    if (!names.insert(name).second)
      return failure(error::invalid_argument, field_path(name), "重复属性名");
  }
  for (const auto& property : properties) {
    if (std::ranges::find(ignored, property.name()) != ignored.end())
      return failure(error::invalid_argument, field_path(property.name()), "反射字段占用格式头");
    auto policy = policy_of(property);
    if (!policy)
      return failure(policy.error(), field_path(property.name()), "字段序列化策略与访问能力不兼容");
    if (!policy->save ||
        !gneiss::meta::has_flags(property.capabilities(),
                                         gneiss::meta::property_capabilities::writable))
      continue;
    auto* input = context_access::input(ctx)->member(root, property.name());
    const auto path = field_path(property.name());
    if (!input) {
      if (policy->required)
        return failure(error::invalid_argument, path, "缺少必填属性");
      if (property.type().kind() != type_kind::scalar) {
        auto child = property.get(view->as_const());
        if (!child)
          return failure(child.error(), path);
        auto status = validate_view(ctx, registry, *child, depth + 1);
        if (status.error.failed())
          return prefix(std::move(status), path);
      }
      continue;
    }
    auto child = property.get(*view);
    if (!child)
      return failure(child.error(), path);
    path_scope property_path{ctx, path};
    auto status = policy->enum_name
                      ? decode_enum_name(ctx, *child, input)
                      : decode_view(ctx, registry, *child, input, depth + 1,
                                    policy->reference ? &*policy->reference : nullptr);
    if (status.error.failed())
      return prefix(std::move(status), path);
  }
  return {};
}

}
