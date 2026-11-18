// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/serialization/context.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/serialization/reader.hpp>
#include <gneiss/engine/core/serialization/serialization.hpp>
#include <map>

namespace gneiss::serialization {
/** 特化提供 representation_type、encode、decode；不改变 Meta 类别或 value 存储。 */
template <class T> struct adapter;
namespace detail {
struct adapter_binding {
  result<writer::node, diagnostic> (*encode)(const write_context&, const meta::const_instance&,
                                             writer&);
  diagnostic (*decode)(const read_context&, const meta::instance&, reader&, reader::node);
};
inline diagnostic adapter_error(error status) { return {.error = status}; }
inline diagnostic adapter_error(diagnostic status) { return status; }
}
/** 启动时配置，操作期间只读；不拥有 Meta 注册表或实例。 */
class adapter_registry {
  template <class T, class A> friend error register_adapter(adapter_registry&);
  std::map<type_id, detail::adapter_binding> entries_;

  error add(type_id id, detail::adapter_binding binding) {
    return entries_.emplace(id, binding).second ? error::success : error::invalid_argument;
  }

public:
  const detail::adapter_binding* find(type_id id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second;
  }
};
namespace detail {
GNEISS_API const adapter_binding* find_adapter(const context_base&, type_id);
}
/** 同类型重复注册返回 invalid_argument；特化必须在调用前可见。 */
template <class T, class A = adapter<T>> error register_adapter(adapter_registry& registry) {
  using R = typename A::representation_type;
  static_assert(!std::is_same_v<T, R>, "Adapter representation must differ from its source");
  return registry.add(
      runtime_type_of<T>(),
      {[](const write_context& ctx, const meta::const_instance& source,
          writer& out) -> result<writer::node, diagnostic> {
         auto convert = [&](const T& native) -> result<writer::node, diagnostic> {
           auto encoded = A::encode(ctx, native);
           if (!encoded)
             return detail::adapter_error(std::move(encoded).error());
           auto view = meta::view(source.registry(), std::as_const(*encoded));
           if (!view)
             return diagnostic{.error = view.error()};
           return write_node(ctx, *view, out);
         };
         if constexpr (meta::detail::reflected_value_kind<T>() != value_kind::invalid) {
           auto value = source.read_value();
           if (!value)
             return diagnostic{.error = value.error()};
           auto native = meta::detail::scalar_cast<T>(*value);
           if (!native)
             return diagnostic{.error = native.error()};
           return convert(*native);
         } else {
           auto address = source.object_data();
           if (!address)
             return diagnostic{.error = address.error()};
           return convert(*static_cast<const T*>(*address));
         }
       },
       [](const read_context& ctx, const meta::instance& target, reader& in,
          reader::node node) -> diagnostic {
         R representation{};
         auto view = meta::view(target.registry(), representation);
         if (!view)
           return {.error = view.error()};
         auto status = read_into(ctx, in, node, *view);
         if (!status)
           return std::move(status).error();
         auto decoded = A::decode(ctx, std::as_const(representation));
         if (!decoded)
           return detail::adapter_error(std::move(decoded).error());
         if constexpr (meta::detail::reflected_value_kind<T>() != value_kind::invalid) {
           return {.error = target.assign(meta::detail::scalar_value(*decoded))};
         } else {
           auto candidate = meta::view(target.registry(), std::as_const(*decoded));
           if (!candidate)
             return {.error = candidate.error()};
           const auto policy = policies_of(candidate->type());
           if (policy.validation.validate &&
               !(ctx.references() && ctx.references()->collecting())) {
             auto checked = policy.validation.validate(std::addressof(*decoded));
             if (checked.error.failed())
               return checked;
           }
           return {.error = target.assign(*candidate)};
         }
       }});
}
}
