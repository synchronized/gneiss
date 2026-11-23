// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/scene/world_state.hpp"
#include <algorithm>
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/logging/logger.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss {
world_state& state_of(world& owner) noexcept {
  auto& state = static_cast<world_state&>(owner);
  GNEISS_ASSERT(state.is_owner_thread(), "World 内部访问要求所属线程");
  return state;
}
const world_state& state_of(const world& owner) noexcept {
  const auto& state = static_cast<const world_state&>(owner);
  GNEISS_ASSERT(state.is_owner_thread(), "World 内部访问要求所属线程");
  return state;
}

void world::register_system(std::string name,
                            std::function<error(world&, const frame_time&)> function) {
  auto& state = state_of(*this);
  GNEISS_ASSERT(!state.systems_frozen_ && !state.destroying_, "World 系统只能在首次更新前注册");
  GNEISS_ASSERT(!name.empty() && function, "World 系统必须具有名称和实现");
  GNEISS_ASSERT(std::none_of(state.systems_.begin(), state.systems_.end(),
                             [&](const auto& entry) { return entry.name == name; }),
                "World 系统名称不能重复", name);
  state.systems_.push_back({std::move(name), std::move(function)});
}
result<void> world::update(const frame_time& time) {
  auto& state = state_of(*this);
  GNEISS_ASSERT(!state.updating_ && !state.destroying_, "World 更新不能重入或在析构时执行");
  state.systems_frozen_ = true;
  state.updating_ = true;
  struct update_scope {
    bool& updating;
    ~update_scope() { updating = false; }
  } scope{state.updating_};
  for (auto& entry : state.systems_) {
    auto status = entry.function(*this, time);
    if (status.failed()) {
      logging::logger{"world"}.write({.severity = logging::level::error,
                                      .source = entry.name,
                                      .category = "system",
                                      .message = "World 系统更新失败",
                                      .result = status});
      return status;
    }
  }
  return {};
}

events::sink<void(const object_changes&)> world::changes() const {
  return state_of(*this).changes.changed();
}
void world::flush_changes() {
  GNEISS_ASSERT(is_owner_thread(), "World 变化通知必须在所属线程派发");
  state_of(*this).changes.flush();
}
result<scene_instance> world::create_scene() {
  if (!is_owner_thread())
    return error::invalid_state;
  const auto frozen = meta::is_frozen();
  if (!frozen || state_of(*this).scene_domain == 0U)
    return error::invalid_state;
  auto created =
      state_of(*this).scene_slots.create(resource_type::scene_instance, scene_instance_id{});
  if (!created)
    return created.error();
  *state_of(*this).scene_slots.get(*created, resource_type::scene_instance) = *created;
  return scene_instance::borrow(*this, *created);
}
bool world::is_owner_thread() const noexcept {
  return static_cast<const world_state&>(*this).is_owner_thread();
}

std::uint64_t world::entity_count() const {
  GNEISS_ASSERT(is_owner_thread(), "World 实体数量必须在所属线程查询");
  return static_cast<std::uint64_t>(state_of(*this).entity_count());
}

result<game_object> world::object(entity_id entity) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  if (!state_of(*this).is_alive(entity.get()))
    return error::invalid_handle;
  if (!state_of(*this).get<detail::object_state>(entity.get()))
    return error::not_found;
  return find(entity);
}
}
