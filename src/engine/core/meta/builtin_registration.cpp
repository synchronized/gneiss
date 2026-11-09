// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/value_edit.hpp"
namespace gneiss::engine::meta {
namespace detail {
error register_math_types(const registry_ref&) noexcept;
}
namespace {
template <class T> const auto& read_content(const T& object) { return object.get(); }
template <class T, class D> error write_content(T& object, const D& data) {
  object = T{data};
  return error::success;
}
}

error register_builtin_types(const registry_ref& registry) noexcept {
  auto status = registration<object>{registry}
                    .metadata(gneiss::engine::serialization::reference_policy{.polymorphic = true})
                    .commit();
  if (status.failed())
    return status;
  status =
      registration<asset_reference_value>{registry}
          .property<&asset_reference_value::type>(property_id{1}, "type", property_flags::required)
          .property<&asset_reference_value::uri>(property_id{2}, "uri", property_flags::required)
          .commit();
  if (status.failed())
    return status;
  status = registration<object_reference_value>{registry}
               .property<&object_reference_value::document>(property_id{1}, "document",
                                                            property_flags::required)
               .property<&object_reference_value::object>(property_id{2}, "object",
                                                          property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status =
      registration<value_property>{registry}
          .property<&value_property::id>(property_id{1}, "id", property_flags::required)
          .property<&value_property::content>(property_id{2}, "content", property_flags::required)
          .commit();
  if (status.failed())
    return status;
  status =
      registration<value_map_entry>{registry}
          .property<&value_map_entry::key>(property_id{1}, "key", property_flags::required)
          .property<&value_map_entry::content>(property_id{2}, "content", property_flags::required)
          .commit();
  if (status.failed())
    return status;
  status = registration<record_value_data>{registry}
               .property<&record_value_data::type>(property_id{1}, "type", property_flags::required)
               .property<&record_value_data::properties>(property_id{2}, "properties",
                                                         property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<sequence_value_data>{registry}
               .property<&sequence_value_data::elements>(property_id{1}, "elements",
                                                         property_flags::required)
               .property<&sequence_value_data::identities>(property_id{2}, "identities")
               .property<&sequence_value_data::fixed>(property_id{3}, "fixed")
               .property<&sequence_value_data::keyed>(property_id{4}, "keyed")
               .commit();
  if (status.failed())
    return status;
  status =
      registration<map_value_data>{registry}
          .property<&map_value_data::entries>(property_id{1}, "entries", property_flags::required)
          .commit();
  if (status.failed())
    return status;
  status = registration<optional_value_data>{registry}
               .property<&optional_value_data::content>(property_id{1}, "content",
                                                        property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<variant_value_data>{registry}
               .property<&variant_value_data::alternative>(property_id{1}, "alternative",
                                                           property_flags::required)
               .property<&variant_value_data::content>(property_id{2}, "content",
                                                       property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status =
      registration<record_value>{registry}
          .property<&read_content<record_value>, &write_content<record_value, record_value_data>>(
              property_id{1}, "data", property_flags::required)
          .commit();
  if (status.failed())
    return status;
  status = registration<sequence_value>{registry}
               .property<&read_content<sequence_value>,
                         &write_content<sequence_value, sequence_value_data>>(
                   property_id{1}, "data", property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<map_value>{registry}
               .property<&read_content<map_value>, &write_content<map_value, map_value_data>>(
                   property_id{1}, "data", property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<optional_value>{registry}
               .property<&read_content<optional_value>,
                         &write_content<optional_value, optional_value_data>>(
                   property_id{1}, "data", property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<variant_value>{registry}
               .property<&read_content<variant_value>,
                         &write_content<variant_value, variant_value_data>>(
                   property_id{1}, "data", property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<set_value_operation>{registry}
               .property<&set_value_operation::content>(property_id{1}, "content",
                                                        property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<insert_value_operation>{registry}
               .property<&insert_value_operation::element>(property_id{1}, "element",
                                                           property_flags::required)
               .property<&insert_value_operation::before>(property_id{2}, "before",
                                                          property_flags::required)
               .property<&insert_value_operation::content>(property_id{3}, "content",
                                                           property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<erase_value_operation>{registry}
               .property<&erase_value_operation::element>(property_id{1}, "element",
                                                          property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<move_value_operation>{registry}
               .property<&move_value_operation::element>(property_id{1}, "element",
                                                         property_flags::required)
               .property<&move_value_operation::before>(property_id{2}, "before",
                                                        property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<put_value_operation>{registry}
               .property<&put_value_operation::key>(property_id{1}, "key", property_flags::required)
               .property<&put_value_operation::content>(property_id{2}, "content",
                                                        property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status = registration<erase_key_operation>{registry}
               .property<&erase_key_operation::key>(property_id{1}, "key", property_flags::required)
               .commit();
  if (status.failed())
    return status;
  status =
      registration<value_edit>{registry}
          .property<&value_edit::path>(property_id{1}, "path", property_flags::required)
          .property<&value_edit::operation>(property_id{2}, "operation", property_flags::required)
          .commit();
  if (status.failed())
    return status;
  return detail::register_math_types(registry);
}
}
