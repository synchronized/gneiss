// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_editor_save.hpp"
#include "../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include "documents/document_editing.hpp"
#include "documents/editor_scene_loader.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/builtin_resource_registration.hpp>
#include <fstream>
#include <iterator>

namespace {

// 独立临时目录，仅删除本用例创建的文件。
struct save_directory {
  std::filesystem::path path;
  ~save_directory() {
    std::error_code error;
    std::filesystem::remove(path / "saved.scene.json", error);
    std::filesystem::remove(path / "failed.scene.json", error);
    std::filesystem::remove(path, error);
  }
};
bool save_codec_contract(gneiss::runtime::application& application) {
  namespace e = gneiss;
  gneiss::guid uuid;
  if (gneiss::editor::make_editor_uuid(uuid).failed())
    return false;
  save_directory directory{std::filesystem::temp_directory_path() /
                           ("gneiss-save-" + uuid.string())};
  if (!std::filesystem::create_directory(directory.path))
    return false;
  gneiss::editor::scene_edit_session document;
  if (document.create_empty(application, application.native_world()).failed())
    return false;
  gneiss::entity_id node;
  if (document.create_node("保存节点", {}, node).failed())
    return false;
  e::math::transform local{};
  local.translation = {2, 3, 4};
  if (document.set_local_transform(node, local).failed())
    return false;
  e::scene_description snapshot;
  gneiss::editor::scene_edit_session closed;
  auto invalid = closed.capture_content();
  if (invalid || invalid.error() != e::error::invalid_state)
    return false;
  std::string saved;
  if (gneiss::test_support::save_document(document, directory.path, &saved,
                                          "asset://saved.scene.json")
          .failed() ||
      document.is_dirty())
    return false;
  gneiss::serialization::diagnostic diagnostic;
  if (gneiss::test_support::assign_decoded(
          gneiss::asset_file::deserialize<std::remove_cvref_t<decltype(snapshot)>>(
              std::as_bytes(std::span{saved.data(), saved.size()})),
          snapshot, diagnostic)
          .failed() ||
      snapshot.objects.size() != 1 || snapshot.objects.front().name != "保存节点" ||
      snapshot.objects.front().transform.translation != local.translation)
    return false;
  // codec 拒绝超限内容时不写文件，不改变 URI、脏状态或调用者的保存结果。
  if (document.rename_node(node, std::string(e::scene_description::maximum_source_bytes, 'x'))
          .failed())
    return false;
  std::string unchanged = "保留输出";
  if (gneiss::test_support::save_document(document, directory.path, &unchanged) !=
          e::error::invalid_argument ||
      gneiss::test_support::save_document(document, directory.path, &unchanged,
                                          "asset://failed.scene.json") !=
          e::error::invalid_argument ||
      unchanged != "保留输出" || !document.is_dirty() ||
      document.uri() != "asset://saved.scene.json" ||
      std::filesystem::exists(directory.path / "failed.scene.json"))
    return false;
  std::ifstream file(directory.path / "saved.scene.json", std::ios::binary);
  const std::string disk{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
  return file.good() && disk == saved;
}

class pending_operation final : public gneiss::editor::editor_history_operation {
public:
  explicit pending_operation(bool& cancelled) : cancelled_(cancelled) {}
  gneiss::error advance(bool& complete) noexcept override {
    complete = ++steps_ == 2;
    return complete && cancelled_ ? gneiss::error::invalid_state
                                  : gneiss::error::success;
  }
  void cancel() noexcept override { cancelled_ = true; }

private:
  bool& cancelled_;
  int steps_{};
};
class pending_command final : public gneiss::editor::editor_command {
public:
  pending_command(gneiss::editor::scene_edit_session& document, bool& cancelled, bool& released)
      : document_(document), cancelled_(cancelled), released_(released) {}
  ~pending_command() override { released_ = document_.is_open(); }
  std::string_view label() const noexcept override { return "关闭顺序验证"; }
  gneiss::error undo() noexcept override { return gneiss::error::invalid_state; }
  gneiss::error redo() noexcept override { return gneiss::error::invalid_state; }
  gneiss::error begin_history_operation(
      bool, std::unique_ptr<gneiss::editor::editor_history_operation>& output) noexcept override {
    try {
      output = std::make_unique<pending_operation>(cancelled_);
    } catch (...) {
      return gneiss::error::out_of_memory;
    }
    return gneiss::error::success;
  }

private:
  gneiss::editor::scene_edit_session& document_;
  bool& cancelled_;
  bool& released_;
};
}
int main() try {
  if (gneiss::initialize_meta().failed())
    return 99;
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create({}), application) !=
      gneiss::error::success)
    return 1;
  if (!save_codec_contract(*application))
    return 12;
  bool cancelled = false;
  bool released_while_open = false;
  gneiss::editor::document_editing editing;
  if (editing.create_empty(*application, application->native_world()).failed())
    return 2;
  const auto scene = editing.document().scene_handle();
  if (editing.history()
          .record(
              std::make_unique<pending_command>(editing.document(), cancelled, released_while_open))
          .failed() ||
      editing.history().undo() != gneiss::error::not_ready || !editing.active())
    return 3;
  // 拒绝关闭不能提前释放历史或场景；取消后仍需排空异步操作。
  if (editing.close() != gneiss::error::not_ready ||
      editing.document().scene_handle() != scene || editing.history().size() != 1 ||
      released_while_open)
    return 4;
  gneiss::editor::editor_scene_loader loader;
  gneiss::editor::editor_camera camera;
  auto world = application->native_world();
  bool changed = true;
  if (editing.create_empty(*application, world) != gneiss::error::not_ready ||
      loader.request(*application, editing, "asset://scene.json") !=
          gneiss::error::not_ready ||
      loader.active() || loader.advance(*application, editing, world, camera, changed).failed() ||
      changed || editing.document().scene_handle() != scene)
    return 8;
  editing.cancel(*application);
  editing.advance_cleanup(*application);
  if (!cancelled || !editing.active() || editing.close() != gneiss::error::not_ready)
    return 5;
  editing.advance_cleanup(*application);
  if (editing.active() || editing.close() != gneiss::error::success ||
      !released_while_open || editing.document().is_open() || editing.history().size() != 0 ||
      editing.close() != gneiss::error::success)
    return 6;
  // 新建失败不能丢失可撤销历史；成功后先释放旧命令，再替换文档。
  if (editing.create_empty(*application, world).failed())
    return 9;
  const auto original = editing.document().scene_handle();
  released_while_open = false;
  if (editing.history()
          .record(
              std::make_unique<pending_command>(editing.document(), cancelled, released_while_open))
          .failed() ||
      editing.create_empty(*application, nullptr) != gneiss::error::invalid_argument ||
      editing.document().scene_handle() != original || !editing.history().can_undo() ||
      released_while_open)
    return 10;
  if (editing.create_empty(*application, world).failed() || !released_while_open ||
      editing.document().scene_handle() == original || editing.history().size() != 0 ||
      !editing.document().is_dirty() || editing.close().failed())
    return 11;
  return 0;
} catch (...) {
  return 7;
}
