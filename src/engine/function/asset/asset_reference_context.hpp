// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/context.hpp"
#include "engine/function/asset/asset_loader.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/resource/asset_uri.hpp"
#include <algorithm>
namespace gneiss::engine {
struct asset_reference_requirement {
  type_id type;
  std::string uri, path;
};
/** 先声明，再由调度器提供稳定身份。序列化阶段只解析已取得的引用，不执行 I/O。 */
class asset_reference_context final : public serialization::reference_resolver {
  struct reference_slot final : asset_dependency {
    ref<void> value;
    reference_slot(type_id type, std::string uri, std::string path)
        : asset_dependency(type, std::move(uri), {}, std::move(path)) {}
    error advance(asset_manager& assets) override {
      if (value)
        return error::success;
      auto loaded = assets.load(type, uri, options);
      if (!loaded) {
        diagnostic = {loaded.error(), 0, path, uri};
        return diagnostic.error;
      }
      value = loaded->native_reference();
      if (!value) {
        diagnostic = {error::unsupported, 0, path, "Loader 未提供稳定身份: " + uri};
        return diagnostic.error;
      }
      return error::success;
    }
  };
  std::vector<asset_reference_requirement> requirements_;
  std::vector<std::shared_ptr<asset_dependency>> slots_;
  bool collecting_{};

public:
  const auto& dependencies() const noexcept { return requirements_; }
  const auto& slots() const noexcept { return slots_; }
  bool collecting() const noexcept override { return collecting_; }
  template <class Decode> serialization::diagnostic collect(Decode decode) {
    if (collecting_)
      return {.error = error::invalid_state};
    collecting_ = true;
    struct guard {
      asset_reference_context& owner;
      std::size_t requirements, slots;
      bool committed{};
      ~guard() {
        owner.collecting_ = false;
        if (!committed) {
          owner.requirements_.resize(requirements);
          owner.slots_.resize(slots);
        }
      }
    } reset{*this, requirements_.size(), slots_.size()};
    serialization::read_context context{*this};
    auto temporary = decode(context);
    reset.committed = bool(temporary);
    return temporary ? serialization::diagnostic{} : std::move(temporary).error();
  }
  serialization::diagnostic record(type_id type, std::string_view uri, std::string_view path) {
    if (validate_uri(uri).failed())
      return {.error = error::invalid_argument};
    if (std::ranges::none_of(requirements_, [&](const auto& entry) {
          return entry.type == type && entry.uri == uri && entry.path == path;
        }))
      requirements_.push_back({type, std::string(uri), std::string(path)});
    if (std::ranges::none_of(
            slots_, [&](const auto& slot) { return slot->type == type && slot->uri == uri; }))
      slots_.push_back(std::make_shared<reference_slot>(type, std::string(uri), std::string(path)));
    return {};
  }
  /** 独立解码调用方的便利入口；Loader 将 slots 交给管理器依赖图推进。 */
  serialization::diagnostic load(asset_manager& assets) {
    for (const auto& slot : slots_) {
      const auto status = slot->advance(assets);
      if (status.failed())
        return {slot->diagnostic.byte_offset, slot->diagnostic.path, slot->diagnostic.message,
                status};
    }
    return {};
  }
  result<ref<void>, serialization::diagnostic> resolve(type_id type, std::string_view uri,
                                                       std::string_view path = {}) override {
    if (collecting_) {
      auto status = record(type, uri, path);
      if (status.error.failed())
        return status;
      return ref<void>{};
    }
    for (const auto& slot : slots_)
      if (slot->type == type && slot->uri == uri) {
        auto value = std::static_pointer_cast<reference_slot>(slot)->value;
        if (value)
          return value;
      }
    return serialization::diagnostic{
        .path = std::string(path), .message = "引用尚未由加载流程提交", .error = error::not_ready};
  }
};
}
