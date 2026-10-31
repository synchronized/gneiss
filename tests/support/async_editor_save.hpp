// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
#include "commands/author_file_save.hpp"
#include "documents/scene_document_snapshot.hpp"
#include "value_result.hpp"
namespace gneiss::test_support {
// 仅测试驱动回调；生产接口没有同步等待包装。
template <class Receipt>
inline engine::error await_file(gneiss::engine::files::service& files,
                                const std::shared_ptr<Receipt>& receipt) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!receipt->complete) {
    if (std::chrono::steady_clock::now() > deadline)
      return engine::error::internal;
    (void)files.dispatch_completions(64);
    std::this_thread::yield();
  }
  return receipt->status;
}
inline engine::error save_document(editor::scene_document& document,
                                   const std::filesystem::path& root, std::string* saved = nullptr,
                                   std::string_view uri = {}) {
  runtime::application_configuration config;
  config.writable_assets = true;
  const auto root_text = root.generic_string();
  config.asset_root = root_text;
  std::shared_ptr<runtime::application> application;
  auto status = runtime::application::create(config, application);
  if (status.failed())
    return status;
  std::shared_ptr<editor::scene_document::save_receipt> receipt;
  status = document.begin_save(*application->async_files(), uri.empty() ? document.uri() : uri,
                               uri.empty() ? gneiss::engine::files::write_mode::replace
                                           : gneiss::engine::files::write_mode::create_new,
                               receipt);
  if (status.ok())
    status = await_file(*application->async_files(), receipt);
  if (status.ok())
    status = document.accept_save(*receipt);
  if (status.ok() && saved)
    *saved = receipt->content;
  return status;
}
struct author_scene {
  std::shared_ptr<runtime::application> application;
  editor::scene_document document;
  editor::scene_document_snapshot before, after;
  bool open(const std::filesystem::path& root, std::string_view uri) {
    runtime::application_configuration config;
    config.writable_assets = true;
    const auto text = root.generic_string();
    config.asset_root = text;
    scene_instance_id scene;
    return runtime::application::create(config, application).ok() &&
           load_scene(*application, uri, &scene).ok() &&
           document.adopt(*application, application->native_world(), scene, uri).ok();
  }
  std::string content() {
    std::string text;
    (void)gneiss::test_support::assign_value(
        application->scenes()->serialize(document.scene_handle()), text);
    return text;
  }
  bool apply(const std::vector<editor::author_document_change>& changes, bool save_source) {
    if (editor::capture_scene_document(*application, document, before).failed() ||
        editor::prepare_scene_document(before, changes, after).failed())
      return false;
    if (save_source) {
      for (const auto& change : changes) {
        if ("asset://" + change.path == document.uri())
          continue;
        std::shared_ptr<editor::author_file_save> receipt;
        if (editor::begin_author_file_save(*application->async_files(), change, receipt).failed() ||
            await_file(*application->async_files(), receipt).failed())
          return false;
      }
    }
    return restore(after);
  }
  bool restore(const editor::scene_document_snapshot& snapshot) {
    if (editor::restore_scene_document(*application, document, snapshot).failed())
      return false;
    document.mark_dirty();
    return true;
  }
  bool counts(std::size_t nodes, std::size_t prefabs) const {
    return document.nodes().size() == nodes && document.prefab_nodes().size() == prefabs;
  }
};
}
