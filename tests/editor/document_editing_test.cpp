// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_editor_save.hpp"
#include "../support/value_result.hpp"
#include "documents/document_editing.hpp"
#include "documents/editor_scene_loader.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "runtime/application.hpp"
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
  namespace e = gneiss::engine;
  gneiss::engine::guid uuid;
  if (gneiss::editor::make_editor_uuid(uuid).failed())
    return false;
  save_directory directory{std::filesystem::temp_directory_path() /
                           ("gneiss-save-" + uuid.string())};
  if (!std::filesystem::create_directory(directory.path))
    return false;
  gneiss::editor::scene_document document;
  if (document.create_empty(application, application.native_world()).failed())
    return false;
  gneiss::scene_node_id node;
  if (document.create_node("保存节点", {}, node).failed())
    return false;
  e::math::transform local{};
  local.translation = {2, 3, 4};
  if (document.set_local_transform(node, local).failed())
    return false;
  e::scene_description snapshot;
  snapshot.uuid = *gneiss::engine::guid::parse("c2df6015-0d50-5e09-a7d1-14d5b9c0232e");
  if (application.scenes()->capture_description(gneiss::scene_instance_id{}, snapshot) !=
          e::error::invalid_handle ||
      snapshot.uuid != *gneiss::engine::guid::parse("c2df6015-0d50-5e09-a7d1-14d5b9c0232e"))
    return false;
  std::string saved;
  if (gneiss::test_support::save_document(document, directory.path, &saved,
                                          "asset://saved.scene.json")
          .failed() ||
      document.is_dirty())
    return false;
  gneiss::engine::meta::registry_ref registry;
  gneiss::engine::meta::serialization_diagnostic diagnostic;
  if (gneiss::test_support::assign_value(e::meta::default_registry(), registry).failed() ||
      gneiss::test_support::assign_decoded(
          gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(snapshot)>>(
              registry, std::as_bytes(std::span{saved.data(), saved.size()})),
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
  gneiss::engine::error advance(bool& complete) noexcept override {
    complete = ++steps_ == 2;
    return complete && cancelled_ ? gneiss::engine::error::invalid_state
                                  : gneiss::engine::error::success;
  }
  void cancel() noexcept override { cancelled_ = true; }

private:
  bool& cancelled_;
  int steps_{};
};
class pending_command final : public gneiss::editor::editor_command {
public:
  pending_command(gneiss::editor::scene_document& document, bool& cancelled, bool& released)
      : document_(document), cancelled_(cancelled), released_(released) {}
  ~pending_command() override { released_ = document_.is_open(); }
  std::string_view label() const noexcept override { return "关闭顺序验证"; }
  gneiss::engine::error undo() noexcept override { return gneiss::engine::error::invalid_state; }
  gneiss::engine::error redo() noexcept override { return gneiss::engine::error::invalid_state; }
  gneiss::engine::error begin_history_operation(
      bool, std::unique_ptr<gneiss::editor::editor_history_operation>& output) noexcept override {
    try {
      output = std::make_unique<pending_operation>(cancelled_);
    } catch (...) {
      return gneiss::engine::error::out_of_memory;
    }
    return gneiss::engine::error::success;
  }

private:
  gneiss::editor::scene_document& document_;
  bool& cancelled_;
  bool& released_;
};
}
int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create({}, application) != gneiss::engine::error::success)
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
      editing.history().undo() != gneiss::engine::error::not_ready || !editing.active())
    return 3;
  // 拒绝关闭不能提前释放历史或场景；取消后仍需排空异步操作。
  if (editing.close() != gneiss::engine::error::not_ready ||
      editing.document().scene_handle() != scene || editing.history().size() != 1 ||
      released_while_open)
    return 4;
  gneiss::editor::editor_scene_loader loader;
  gneiss::editor::editor_camera camera;
  auto world = application->native_world();
  bool changed = true;
  if (editing.create_empty(*application, world) != gneiss::engine::error::not_ready ||
      loader.request(*application, editing, "asset://scene.json") !=
          gneiss::engine::error::not_ready ||
      loader.active() || loader.advance(*application, editing, world, camera, changed).failed() ||
      changed || editing.document().scene_handle() != scene)
    return 8;
  editing.cancel(*application);
  editing.advance_cleanup(*application);
  if (!cancelled || !editing.active() || editing.close() != gneiss::engine::error::not_ready)
    return 5;
  editing.advance_cleanup(*application);
  if (editing.active() || editing.close() != gneiss::engine::error::success ||
      !released_while_open || editing.document().is_open() || editing.history().size() != 0 ||
      editing.close() != gneiss::engine::error::success)
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
      editing.create_empty(*application, nullptr) != gneiss::engine::error::invalid_argument ||
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
