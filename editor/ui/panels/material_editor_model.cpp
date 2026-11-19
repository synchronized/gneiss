// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "ui/panels/material_editor_model.hpp"
#include <gneiss/engine/core/meta/resolve_path.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/asset_uri.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
namespace gneiss::editor {
using namespace gneiss;
error material_editor_model::open(std::string uri, std::string content) {
  if (saving() || is_dirty())
    return error::invalid_state;
  if (!uri.starts_with("asset://") || validate_uri(uri).failed())
    return error::invalid_argument;
  auto decoded = gneiss::asset_file::deserialize<material_description>(
      std::as_bytes(std::span{content.data(), content.size()}));
  if (!decoded)
    return decoded.error().error;
  auto encoded = gneiss::asset_file::serialize_bytes(*decoded);
  if (!encoded)
    return encoded.error().error;
  auto next = std::make_shared<snapshot>(
      snapshot{std::move(*decoded),
               std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size())});
  auto baseline = std::make_shared<const std::string>(std::move(content));
  auto status = history_.clear();
  if (status.failed())
    return status;
  uri_ = std::move(uri);
  current_ = saved_ = std::move(next);
  baseline_ = std::move(baseline);
  save_status_ = error::success;
  return error::success;
}
error material_editor_model::set_value(const material_property_path& path, const value& input,
                                       std::uint64_t serial) noexcept {
  return apply(path, &input, nullptr, serial);
}
error material_editor_model::replace_object(const property_path& path,
                                            const meta::const_instance& source) noexcept {
  return apply(path, nullptr, &source, 0);
}
error material_editor_model::apply(const property_path& path, const value* input,
                                   const meta::const_instance* source, std::uint64_t serial) {
  if (input && input->kind() == value_kind::object)
    return error::unsupported;
  if (!current_ || path.empty())
    return error::invalid_argument;
  auto candidate = current_->description;
  auto root = meta::view(candidate);
  std::string key = gneiss::property_path_text(path);
  auto target = meta::resolve_path(root, path);
  if (!target)
    return target.error();
  // 对象替换是编辑器操作约束，不能依赖通用实例赋值拒绝其他类型。
  if (source && (target->type().kind() != meta::type_kind::object ||
                 source->type().kind() != meta::type_kind::object))
    return error::unsupported;
  auto status = source ? target->assign(*source) : target->assign(*input);
  if (status.failed())
    return status;
  for (const auto& uri : candidate.texture_uris)
    if (!uri.empty() && validate_uri(uri).failed())
      return error::invalid_argument;
  // 文件保存边界执行领域校验后编码，不发布半成品。
  auto encoded = gneiss::asset_file::serialize_bytes(candidate);
  if (!encoded)
    return encoded.error().error;
  auto next = std::make_shared<const snapshot>(
      snapshot{std::move(candidate),
               std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size())});
  if (next->encoded == current_->encoded)
    return error::success;
  return history_.execute({.label = "修改材质属性",
                           .undo =
                               [this, before = current_] {
                                 current_ = before;
                                 return error::success;
                               },
                           .redo =
                               [this, next] {
                                 current_ = next;
                                 return error::success;
                               },
                           .merge_key = serial ? std::to_string(serial) + key : std::string{}});
}
bool material_editor_model::is_dirty() const noexcept {
  return current_ && saved_ && current_->encoded != saved_->encoded;
}
error material_editor_model::discard() noexcept {
  if (saving())
    return error::not_ready;
  auto status = history_.clear();
  if (status.ok())
    current_ = saved_;
  return status;
}
error material_editor_model::begin_save(files::service& files) {
  if (!current_ || saving())
    return error::invalid_state;
  auto status = begin_author_file_save(
      files, {.path = uri_.substr(8), .baseline = *baseline_, .replacement = current_->encoded},
      receipt_);
  save_status_ = status;
  if (status.ok()) {
    pending_ = current_;
    save_status_ = error::not_ready;
  }
  return status;
}
void material_editor_model::advance() {
  if (!receipt_ || !receipt_->complete)
    return;
  save_status_ = receipt_->status;
  if (save_status_.ok()) {
    saved_ = pending_;
    baseline_ = std::shared_ptr<const std::string>(saved_, &saved_->encoded);
  }
  pending_.reset();
  receipt_.reset();
}
}
