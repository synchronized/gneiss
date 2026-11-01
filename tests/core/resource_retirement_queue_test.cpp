// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/asset_ref.hpp"
#include "engine/resource/resource_retirement_queue.hpp"

#include <atomic>
#include <barrier>
#include <memory>
#include <new>
#include <thread>
#include <vector>

using gneiss::engine::resource_retirement_queue;
namespace {
struct counters {
  std::atomic_uint destroyed{};
  std::atomic_uint attached{};
  std::atomic_bool wrong_thread{};
  std::thread::id owner{std::this_thread::get_id()};
};
struct resource {
  counters& counts;
  std::shared_ptr<resource> dependency;
  explicit resource(counters& value) : counts(value) {}
  ~resource() { ++counts.destroyed; }
};
void finalize(resource& value, void* owner) noexcept {
  if (owner) {
    ++value.counts.attached;
    if (std::this_thread::get_id() != value.counts.owner)
      value.counts.wrong_thread = true;
  }
}
} // namespace

int main() {
  using namespace gneiss::engine;
  static_assert(std::is_same_v<asset_ref<resource>, ref<resource>>);
  static_assert(!std::is_constructible_v<ref<resource>, resource*>);
  static_assert(!std::is_constructible_v<ref<resource>, ref<const resource>>);
  // 类型擦除、借用别名和跨线程释放共用原控制块及删除器。
  {
    counters tracked;
    resource_retirement_queue retirement(&tracked);
    ref<resource> owner{retirement.make<resource>(finalize, tracked)};
    std::weak_ptr<resource> weak = owner.shared();
    ref<const void> erased = owner;
    auto alias = owner.alias(&owner->counts);
    owner.reset();
    erased.reset();
    if (weak.expired() || tracked.destroyed != 0U)
      return 12;
    std::jthread release([last = std::move(alias)]() mutable { last.reset(); });
    release.join();
    if (!weak.expired() || tracked.destroyed != 0U || retirement.drain() != 1U ||
        tracked.destroyed != 1U || tracked.wrong_thread)
      return 13;
  }
  {
    auto original = make_ref<int>(42);
    ref<const int> moved = std::move(original);
    if (original || !moved || *moved != 42 || moved.use_count() != 1)
      return 14;
    ref<void> empty;
    if (empty || empty.alias(static_cast<int*>(nullptr)))
      return 15;
  }

  counters counts;
  resource_retirement_queue queue(&counts);
  // 工厂构造失败不能留下待销毁节点；队列随后仍可正常使用。
  try {
    (void)queue.make_with_factory<resource>(finalize, []() -> resource { throw std::bad_alloc{}; });
    return 10;
  } catch (const std::bad_alloc&) {
    if (queue.drain() != 0U || counts.destroyed != 0U)
      return 11;
  }
  auto texture = queue.make<resource>(finalize, counts);
  auto material = queue.make<resource>(finalize, counts);
  material->dependency = std::move(texture);
  std::weak_ptr<resource> weak = material;
  std::jthread worker([value = std::move(material)]() mutable { value.reset(); });
  worker.join();
  // 强引用已归零，但析构及依赖释放必须等主线程消费。
  if (!weak.expired() || counts.destroyed != 0U)
    return 1;
  if (queue.drain() != 2U || counts.destroyed != 2U || counts.attached != 2U || counts.wrong_thread)
    return 2;
  // 主线程最后释放也只入队，避免同步递归析构。
  queue.make<resource>(finalize, counts).reset();
  if (counts.destroyed != 2U || queue.drain() != 1U)
    return 3;

  // 多生产者与 close 竞争：要么入队后在所属线程销毁，要么走 detached 路径。
  constexpr unsigned producer_count = 8U;
  std::barrier gate(producer_count + 1U);
  std::vector<std::jthread> workers;
  for (unsigned index = 0U; index < producer_count; ++index) {
    auto value = queue.make<resource>(finalize, counts);
    workers.emplace_back([value = std::move(value), &gate]() mutable {
      gate.arrive_and_wait();
      value.reset();
    });
  }
  auto late = queue.make<resource>(finalize, counts);
  gate.arrive_and_wait();
  queue.close();
  workers.clear();
  if (counts.destroyed != 3U + producer_count || counts.wrong_thread)
    return 4;
  const auto before = counts.attached.load();
  std::jthread late_worker([value = std::move(late)]() mutable { value.reset(); });
  late_worker.join();
  if (counts.destroyed != 4U + producer_count || counts.attached != before)
    return 5;

  // 端点可以晚于队列和服务销毁，依赖级联也不访问旧服务。
  std::shared_ptr<resource> external;
  {
    resource_retirement_queue temporary(&counts);
    external = temporary.make<resource>(finalize, counts);
    external->dependency = temporary.make<resource>(finalize, counts);
  }
  std::jthread detached([value = std::move(external)]() mutable { value.reset(); });
  detached.join();
  return counts.destroyed == 6U + producer_count && counts.attached == before ? 0 : 6;
}
