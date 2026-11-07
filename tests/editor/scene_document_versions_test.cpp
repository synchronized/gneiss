// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_versions.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/scene/detail/scene_asset_access.hpp"
#include <cstdio>
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
  e::object_description object;
  object.uuid = e::guid::literal("4e62c312-7871-4029-9f19-66af826a0c33");
  object.name = "initial";
  input.description.objects.push_back(object);
  auto snapshot = ed::make_scene_document_snapshot("asset://test.scene.json", input);
  CHECK(snapshot);
  ed::scene_document_versions versions(2);
  CHECK(!versions.current());
  CHECK(versions.undo().failed());
  CHECK(versions.reset(*snapshot).ok());
  const auto initial = versions.current();
  CHECK(!versions.is_dirty() && !versions.is_synchronized());
  CHECK(versions.report_projection(initial, e::error::success).ok());
  CHECK(versions.is_synchronized());
  const auto renamed = [&](std::string name) {
    auto next = versions.current().content();
    auto data = next.content.get();
    data.objects[0].name = std::move(name);
    next.content = e::scene_record{data};
    return next;
  };
  auto candidate = renamed("first");
  CHECK(versions.accept(initial, candidate, "rename", "name").ok());
  const auto first = versions.current();
  candidate.uri = "changed externally";
  CHECK(first.content().uri == "asset://test.scene.json");
  CHECK(initial.content().content.get().objects[0].name == "initial");
  CHECK(versions.is_dirty() && !versions.is_synchronized());
  CHECK(versions.report_projection(initial, e::error::success) == e::error::invalid_state);
  CHECK(versions.report_projection(first, e::error::io).ok());
  CHECK(versions.projection_status() == e::error::io && versions.current() == first);
  // 投影失败仍能从当前版本保存内容；这里没有 Application、World 或文件服务。
  auto description = first.content().description();
  CHECK(description && description->objects[0].name == "first");
  CHECK(e::asset_file::serialize_bytes(*description));
  CHECK(versions.report_projection(first, e::error::success).ok());
  CHECK(versions.is_synchronized());
  CHECK(versions.accept(first, renamed("second"), "rename again", "name").ok());
  CHECK(versions.size() == 1 && versions.undo_label() == "rename again");
  const auto second = versions.current();
  CHECK(versions.mark_saved(second).ok());
  CHECK(!versions.is_dirty());
  CHECK(versions.accept(second, renamed("third"), "after save", "name").ok());
  CHECK(versions.size() == 2); // 不能跨越保存点合并。
  CHECK(versions.undo().ok() && versions.current() == second && !versions.is_dirty());
  CHECK(!versions.is_synchronized());
  CHECK(versions.undo().ok() && versions.current() == initial && versions.is_dirty());
  CHECK(versions.redo().ok() && versions.current() == second);
  const auto invalid = [&] {
    auto result = renamed("invalid");
    auto data = result.content.get();
    data.objects[0].parent = data.objects[0].uuid;
    result.content = e::scene_record{data};
    return result;
  }();
  CHECK(versions.accept(second, invalid, "cycle").failed());
  CHECK(versions.current() == second && versions.can_redo());
  CHECK(versions.reset(invalid).failed() && versions.current() == second);
  CHECK(versions.accept(initial, renamed("stale"), "stale").failed());
  CHECK(versions.accept(second, renamed("branch"), "branch").ok());
  CHECK(!versions.can_redo());
  const auto branch = versions.current();
  // 保存旧版本的异步完成不能清除后续编辑的 dirty。
  CHECK(versions.mark_saved(second).ok() && versions.is_dirty());
  CHECK(versions.accept(branch, renamed("fourth"), "fourth").ok());
  CHECK(versions.size() == 2);
  CHECK(versions.undo().ok() && versions.current() == branch);
  CHECK(versions.undo().ok() && versions.current() == second);
  CHECK(!versions.can_undo() && !versions.is_dirty());
  // 保存点与同步状态不额外保活已移出历史的整个版本。
  versions.clear_history();
  CHECK(versions.current() == second && !versions.can_undo() && !versions.can_redo());
  CHECK(versions.reset(*snapshot, false).ok());
  CHECK(versions.is_dirty());
  CHECK(versions.mark_saved(second).failed());
  CHECK(versions.report_projection(second, e::error::success).failed());
  ed::scene_document_versions other;
  CHECK(other.reset(*snapshot).ok());
  CHECK(versions.mark_saved(other.current()).failed());
  CHECK(versions.accept(other.current(), *snapshot, "foreign").failed());
  ed::scene_document_versions no_history(0);
  CHECK(no_history.reset(*snapshot).ok());
  CHECK(no_history.accept(no_history.current(), renamed("unrecorded"), "edit").ok());
  CHECK(no_history.size() == 0 && no_history.is_dirty() && !no_history.can_undo());
  const auto edited = no_history.current();
  CHECK(no_history.checkout(edited, other.current()) == e::error::invalid_state);
  CHECK(no_history.checkout(initial, edited) == e::error::invalid_state);
  CHECK(no_history.current() == edited);
  CHECK(no_history.accept(edited, renamed("next"), "edit").ok());
  const auto next = no_history.current();
  CHECK(no_history.checkout(next, edited).ok() && no_history.current() == edited);
  CHECK(!no_history.is_synchronized() && no_history.size() == 0);
  CHECK(no_history.checkout(edited, next).ok() && no_history.current() == next);
  // 历史专有资产只由旧版本保活；保存点、同步回执不延长它的寿命。
  ed::scene_document_versions retention;
  std::weak_ptr<const e::scene> retired_asset;
  {
    auto tagged = *snapshot;
    auto content = e::make_ref<const e::scene_content>(
        e::scene_content{e::make_ref<const e::scene_description>(input.description), {}});
    auto asset = e::detail::scene_asset_access::make<e::scene, e::scene_content>({}, content);
    retired_asset = asset;
    tagged.source_asset = e::ref<const e::scene>{std::move(asset)};
    CHECK(retention.reset(tagged).ok());
    CHECK(retention.report_projection(retention.current(), e::error::success).ok());
  }
  CHECK(retention.accept(retention.current(), *snapshot, "replace source").ok());
  CHECK(!retired_asset.expired());
  retention.clear_history();
  CHECK(retired_asset.expired());
  return 0;
}
