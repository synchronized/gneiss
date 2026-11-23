// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "instance_access.hpp"
#include <gneiss/engine/core/meta/resolve_path.hpp>

namespace gneiss::meta {
result<value> read_path(const const_instance& root, std::span<const property_step> path,
                        bool persistent) {
  if (!root.type())
    return error::invalid_argument;
  if (path.empty())
    return root.read_value();
  result<value> output = error::unsupported;
  auto status = detail::instance_access::read(root, [&](const const_instance& source) {
    auto child = resolve_path(source, path.first(1), persistent);
    output = child ? read_path(*child, path.subspan(1), persistent) : result<value>{child.error()};
    return error::success;
  });
  return status.failed() ? result<value>{status} : output;
}

error write_path(const instance& root, std::span<const property_step> path, const value& input,
                 bool persistent) {
  if (!root.type())
    return error::invalid_argument;
  if (path.empty())
    return root.assign(input);
  if (detail::instance_access::constrained(root)) {
    return detail::instance_access::edit(root, [&](const instance& candidate) {
      return write_path(candidate, path, input, persistent);
    });
  }
  auto child = resolve_path(root, path.first(1), persistent);
  return child ? write_path(*child, path.subspan(1), input, persistent) : child.error();
}
}
