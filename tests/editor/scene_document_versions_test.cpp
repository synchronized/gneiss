// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/document_patch.hpp"
#include "documents/scene_document.hpp"
#include "documents/scene_projection.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/scene/detail/scene_asset_access.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
namespace {
thread_local std::size_t fail_allocation_at_least = 0;
}
void* operator new(std::size_t size) {
  if (fail_allocation_at_least && size >= fail_allocation_at_least) {
    fail_allocation_at_least = 0;
    throw std::bad_alloc{};
  }
  if (auto* memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                                \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)
int main() {
  CHECK(e::initialize_meta().ok());
  e::scene_instantiation_data input;
  input.description.uuid = e::guid::literal("a5352685-908d-459f-85f4-a866df229a10");
  e::game_object_description object;
  object.uuid = e::guid::literal("4e62c312-7871-4029-9f19-66af826a0c33");
  object.name = "initial";
  input.description.objects.push_back(object);
  auto snapshot = ed::make_scene_document_snapshot("asset://test.scene.json", input);
  CHECK(snapshot);
  ed::scene_document document;
  ed::scene_projection projection;
  CHECK(!document.current());
  CHECK(document.reset(*snapshot).ok());
  const auto initial = document.current();
  CHECK(!document.is_dirty() && !projection.is_synchronized(initial));
  CHECK(projection.report(initial, initial, e::error::success).ok());
  CHECK(projection.is_synchronized(initial));
  auto edited = *snapshot;
  auto data = edited.content.get();
  data.objects[0].name = "edited";
  edited.content = e::scene_record{data};
  {
    auto candidate = document.make_candidate();
    CHECK(candidate);
    CHECK((*candidate)->accept(initial, edited).ok());
    CHECK(document.current() == initial && !document.is_dirty());
    const auto prepared = (*candidate)->current();
    CHECK(document.checkout(initial, prepared).ok());
    CHECK(document.current() == prepared && document.is_dirty());
    CHECK(document.checkout(prepared, initial).ok());
  }
  // 注入准备阶段真实分配失败；候选和历史差异失败均不发布到权威文档。
  fail_allocation_at_least = sizeof(ed::scene_document);
  auto failed_candidate = document.make_candidate();
  fail_allocation_at_least = 0;
  CHECK(!failed_candidate && failed_candidate.error() == e::error::out_of_memory);
  CHECK(document.current() == initial && !document.is_dirty());
  auto patch_before = initial.content();
  auto patch_after = edited;
  patch_before.uri = patch_after.uri = "asset://" + std::string(256, 'a');
  // 跳过 MSVC 调试 STL 在 noexcept 默认构造中分配的迭代器代理，定向失败 URI 存储。
  fail_allocation_at_least = 256;
  auto failed_patch = ed::make_document_patch(patch_before, patch_after);
  fail_allocation_at_least = 0;
  CHECK(!failed_patch && failed_patch.error() == e::error::out_of_memory);
  CHECK(document.current() == initial && !document.is_dirty());
  CHECK(document.accept(initial, edited).ok());
  const auto next = document.current();
  CHECK(document.is_dirty() && !projection.is_synchronized(next));
  CHECK(projection.report(next, initial, e::error::success) == e::error::invalid_state);
  CHECK(projection.report(next, next, e::error::io).ok());
  CHECK(projection.status(next) == e::error::io);
  CHECK(document.current().content().content.get().objects[0].name == "edited");
  CHECK(initial.content().content.get().objects[0].name == "initial");
  CHECK(document.mark_saved(initial).ok() && document.is_dirty());
  CHECK(document.mark_saved(next).ok() && !document.is_dirty());
  CHECK(document.checkout(next, initial).ok() && document.is_dirty());
  CHECK(document.checkout(initial.stamp(), next.stamp(), edited).ok() && !document.is_dirty());
  CHECK(!projection.is_synchronized(document.current()));
  CHECK(projection.report(document.current(), document.current(), e::error::success).ok());
  CHECK(projection.is_synchronized(document.current()));
  auto invalid = edited;
  data.objects[0].parent = data.objects[0].uuid;
  invalid.content = e::scene_record{data};
  CHECK(document.accept(document.current(), invalid).failed());
  CHECK(document.reset(invalid).failed());
  CHECK(document.current() == next);
  CHECK(document.accept(initial, edited) == e::error::invalid_state);
  ed::scene_document other;
  CHECK(other.reset(*snapshot).ok());
  CHECK(document.mark_saved(other.current()).failed());
  CHECK(document.checkout(next, other.current()).failed());
  CHECK(document.accept(other.current(), edited).failed());
  CHECK(document.reset(*snapshot, false).ok() && document.is_dirty());
  CHECK(document.mark_saved(next).failed());
  CHECK(projection.report(document.current(), next, e::error::success).failed());
  // 文档自身不保留第二套历史；保存点和投影回执不保活旧内容及资源。
  ed::scene_document retention;
  ed::scene_projection retained_projection;
  std::weak_ptr<const e::scene> retired_asset;
  {
    auto tagged = *snapshot;
    auto content = e::make_ref<const e::scene_content>(
        e::scene_content{e::make_ref<const e::scene_description>(input.description), {}});
    auto asset = e::detail::scene_asset_access::make<e::scene, e::scene_content>({}, content);
    retired_asset = asset;
    tagged.source_asset = e::ref<const e::scene>{std::move(asset)};
    CHECK(retention.reset(tagged).ok());
    CHECK(retained_projection.report(retention.current(), retention.current(), e::error::success)
              .ok());
  }
  CHECK(retention.accept(retention.current(), *snapshot).ok());
  CHECK(retired_asset.expired());
  return 0;
}
