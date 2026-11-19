// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_document.hpp"
#include "documents/commands/author_file_save.hpp"
#include "documents/scene_document_edit.hpp"
#include "scene_query.hpp"
#include "value_result.hpp"
#include <cstdio>
namespace gneiss::test_support {
// 仅测试驱动回调；生产接口没有同步等待包装。
template <class Receipt>
inline gneiss::error await_file(gneiss::files::service& files,
                                const std::shared_ptr<Receipt>& receipt) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!receipt->complete) {
    if (std::chrono::steady_clock::now() > deadline)
      return gneiss::error::internal;
    if (const auto dispatched = files.dispatch_completions(64); dispatched.failed())
      return dispatched;
    std::this_thread::yield();
  }
  return receipt->status;
}
inline gneiss::error save_document(editor::scene_edit_session& document,
                                   const std::filesystem::path& root, std::string* saved = nullptr,
                                   std::string_view uri = {}) {
  runtime::application_configuration config;
  config.executor = &gneiss::test_support::application_tasks();
  config.writable_assets = true;
  const auto root_text = root.generic_string();
  config.asset_root = root_text;
  std::shared_ptr<runtime::application> application;
  auto status =
      gneiss::test_support::assign_value(runtime::application::create(config), application);
  if (status.failed())
    return status;
  std::shared_ptr<editor::scene_edit_session::save_receipt> receipt;
  status = document.begin_save(*application->async_files(), uri.empty() ? document.uri() : uri,
                               uri.empty() ? gneiss::files::write_mode::replace
                                           : gneiss::files::write_mode::create_new,
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
  gneiss::world_owner world;
  editor::scene_edit_session document;
  editor::scene_document_snapshot before, after;
  bool open(const std::filesystem::path& root, std::string_view uri) {
    runtime::application_configuration config;
    config.executor = &gneiss::test_support::application_tasks();
    config.writable_assets = true;
    const auto text = root.generic_string();
    config.asset_root = text;
    auto status =
        gneiss::test_support::assign_value(runtime::application::create(config), application);
    if (status.ok())
      status = assign_value(gneiss::create_native(), world);
    if (status.ok())
      status = open_document(document, *application, world.get(), uri);
    if (status.failed())
      std::fprintf(stderr, "打开文档失败: %s (%d)\n", std::string(uri).c_str(), status.code());
    return status.ok();
  }
  std::string content() {
    auto text = document.serialize();
    if (!text)
      throw std::runtime_error("序列化测试文档失败");
    return std::move(*text);
  }
  bool apply(const std::vector<editor::author_document_change>& changes, bool save_source) {
    auto captured = editor::capture_scene_document(*application, document);
    if (!captured)
      return false;
    auto prepared = editor::prepare_scene_document(*captured, changes);
    if (!prepared || !prepared->missing.empty())
      return false;
    before = std::move(*captured);
    after = std::move(prepared->snapshot);
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
    const auto snapshot = gneiss::test_support::scene_query(document);
    return snapshot.nodes.size() == nodes && snapshot.prefab_nodes.size() == prefabs;
  }
};
}
