// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/core/value_edit.hpp>
namespace gneiss::serialization {
using namespace meta;
namespace detail {
error register_math_types(const registry_ref&);
}
namespace {
template <class T> const auto& read_content(const T& object) { return object.get(); }
template <class T, class D> error write_content(T& object, const D& data) {
  object = T{data};
  return error::success;
}
}

error register_builtin_types(const registry_ref& registry) {
  auto status =
      registration::type<object>(registry).metadata(reference_policy{.polymorphic = true}).commit();
  if (status.failed())
    return status;
  status = registration::type<asset_reference_value>(registry)
               .property<&asset_reference_value::type>(property_id{1},
                                                       "type")(field_policy{.required = true})
               .property<&asset_reference_value::uri>(property_id{2},
                                                      "uri")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<object_reference_value>(registry)
               .property<&object_reference_value::document>(property_id{1}, "document")(
                   field_policy{.required = true})
               .property<&object_reference_value::object>(property_id{2},
                                                          "object")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<value_property>(registry)
               .property<&value_property::id>(property_id{1}, "id")(field_policy{.required = true})
               .property<&value_property::content>(property_id{2},
                                                   "content")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status =
      registration::type<value_map_entry>(registry)
          .property<&value_map_entry::key>(property_id{1}, "key")(field_policy{.required = true})
          .property<&value_map_entry::content>(property_id{2},
                                               "content")(field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status = registration::type<record_value_data>(registry)
               .property<&record_value_data::type>(property_id{1},
                                                   "type")(field_policy{.required = true})
               .property<&record_value_data::properties>(property_id{2}, "properties")(
                   field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<sequence_value_data>(registry)
               .property<&sequence_value_data::elements>(property_id{1},
                                                         "elements")(field_policy{.required = true})
               .property<&sequence_value_data::identities>(property_id{2}, "identities")
               .property<&sequence_value_data::fixed>(property_id{3}, "fixed")
               .property<&sequence_value_data::keyed>(property_id{4}, "keyed")
               .commit();
  if (status.failed())
    return status;
  status = registration::type<map_value_data>(registry)
               .property<&map_value_data::entries>(property_id{1},
                                                   "entries")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<optional_value_data>(registry)
               .property<&optional_value_data::content>(property_id{1},
                                                        "content")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<variant_value_data>(registry)
               .property<&variant_value_data::alternative>(property_id{1}, "alternative")(
                   field_policy{.required = true})
               .property<&variant_value_data::content>(property_id{2},
                                                       "content")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status =
      registration::type<record_value>(registry)
          .property<&read_content<record_value>, &write_content<record_value, record_value_data>>(
              property_id{1}, "data")(field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status = registration::type<sequence_value>(registry)
               .property<&read_content<sequence_value>,
                         &write_content<sequence_value, sequence_value_data>>(
                   property_id{1}, "data")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<map_value>(registry)
               .property<&read_content<map_value>, &write_content<map_value, map_value_data>>(
                   property_id{1}, "data")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<optional_value>(registry)
               .property<&read_content<optional_value>,
                         &write_content<optional_value, optional_value_data>>(
                   property_id{1}, "data")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<variant_value>(registry)
               .property<&read_content<variant_value>,
                         &write_content<variant_value, variant_value_data>>(property_id{1}, "data")(
                   field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<set_value_operation>(registry)
               .property<&set_value_operation::content>(property_id{1},
                                                        "content")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<insert_value_operation>(registry)
               .property<&insert_value_operation::element>(property_id{1}, "element")(
                   field_policy{.required = true})
               .property<&insert_value_operation::before>(property_id{2},
                                                          "before")(field_policy{.required = true})
               .property<&insert_value_operation::content>(property_id{3}, "content")(
                   field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<erase_value_operation>(registry)
               .property<&erase_value_operation::element>(property_id{1},
                                                          "element")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<move_value_operation>(registry)
               .property<&move_value_operation::element>(property_id{1},
                                                         "element")(field_policy{.required = true})
               .property<&move_value_operation::before>(property_id{2},
                                                        "before")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<put_value_operation>(registry)
               .property<&put_value_operation::key>(property_id{1},
                                                    "key")(field_policy{.required = true})
               .property<&put_value_operation::content>(property_id{2},
                                                        "content")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<erase_key_operation>(registry)
               .property<&erase_key_operation::key>(property_id{1},
                                                    "key")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = registration::type<value_edit>(registry)
               .property<&value_edit::path>(property_id{1}, "path")(field_policy{.required = true})
               .property<&value_edit::operation>(property_id{2},
                                                 "operation")(field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return detail::register_math_types(registry);
}
}
