// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "panels/material_editor_model.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/material/material_codec.hpp"
namespace gneiss::editor {
using namespace engine;
error material_editor_model::open(std::string uri, std::string content) noexcept try {
  if (saving() || is_dirty())
    return error::invalid_state;
  if (!uri.starts_with("asset://") || validate_uri(uri).failed())
    return error::invalid_argument;
  auto decoded = decode_material(std::as_bytes(std::span{content.data(), content.size()}));
  if (!decoded)
    return decoded.error().error;
  auto encoded = encode_material(*decoded);
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
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error material_editor_model::set_value(const material_property_path& path, const value& input,
                                       std::uint64_t serial) noexcept {
  return apply(path, &input, nullptr, serial);
}
error material_editor_model::replace_object(const property_path& path,
                                            const meta::instance& source) noexcept {
  return apply(path, nullptr, &source, 0);
}
error material_editor_model::apply(const property_path& path, const value* input,
                                   const meta::instance* source, std::uint64_t serial) noexcept
    try {
  if (!current_ || path.empty())
    return error::invalid_argument;
  auto candidate = current_->description;
  auto target = meta::mutable_instance::from(candidate);
  if (!target)
    return target.error();
  std::string key;
  for (const auto& step : path) {
    if (const auto* property = std::get_if<meta::property_id>(&step)) {
      target = target->property(*property);
      key += "/p" + std::to_string(property->get());
    } else {
      auto sequence = target->as_sequence();
      if (!sequence)
        return sequence.error();
      const auto index = std::get<std::size_t>(step);
      target = sequence->at(index);
      key += "/i" + std::to_string(index);
    }
    if (!target)
      return target.error();
  }
  auto status = source ? target->assign(*source) : target->write_value(*input);
  if (status.failed())
    return status;
  for (const auto& uri : candidate.texture_uris)
    if (!uri.empty() && validate_uri(uri).failed())
      return error::invalid_argument;
  // 完整编码同时执行嵌套类型和整材质校验，不发布半成品。
  auto encoded = encode_material(candidate);
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
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
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
error material_editor_model::begin_save(files::service& files) noexcept try {
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
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
void material_editor_model::advance() noexcept {
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
