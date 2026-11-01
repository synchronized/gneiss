// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/async_editor_save.hpp"
#include <fstream>
#include <iterator>
namespace {
std::string read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
}
int main() try {
  using namespace gneiss;
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-author-save-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  runtime::application_configuration config;
  config.writable_assets = true;
  const auto text = root.generic_string();
  config.asset_root = text;
  std::shared_ptr<runtime::application> app;
  if (runtime::application::create(config, app).failed())
    return 1;
  auto files = app->async_files();
  auto save = [&](editor::author_document_change change) {
    std::shared_ptr<editor::author_file_save> receipt;
    auto status = editor::begin_author_file_save(*files, std::move(change), receipt);
    if (status.ok()) {
      if (receipt->complete)
        return engine::error::internal; // 业务完成不得内联。
      status = test_support::await_file(*files, receipt);
    }
    return status;
  };
  if (save({"source.prefab.json", {}, "one"}).failed() ||
      save({"source.prefab.json", {}, "clobber"}) != engine::error::invalid_state ||
      read(root / "source.prefab.json") != "one" ||
      save({"source.prefab.json", "one", "two"}).failed() ||
      save({"source.prefab.json", "one", "stale"}) != engine::error::invalid_state ||
      save({"source.prefab.json", "two", {}}) != engine::error::invalid_argument ||
      read(root / "source.prefab.json") != "two")
    return 2;
  std::filesystem::create_directory(root / "directory");
  if (save({"directory", {}, "bad"}).ok())
    return 3;
  for (const auto& entry : std::filesystem::directory_iterator(root))
    if (entry.path().filename().string().find(".gneiss-") != std::string::npos)
      return 4;
  editor::scene_document document;
  scene_node_id node;
  if (document.create_empty(*app, app->native_world()).failed() ||
      document.create_node("before", {}, node).failed())
    return 5;
  std::shared_ptr<editor::scene_document::save_receipt> receipt;
  if (document
          .begin_save(*files, "asset://scene.json", gneiss::engine::files::write_mode::create_new,
                      receipt)
          .failed() ||
      receipt->complete || document.rename_node(node, "after").failed() ||
      test_support::await_file(*files, receipt).failed() ||
      document.accept_save(*receipt).failed() || !document.is_dirty() ||
      read(root / "scene.json").find("before") == std::string::npos)
    return 6;
  if (document
          .begin_save(*files, document.uri(), gneiss::engine::files::write_mode::replace, receipt)
          .failed())
    return 7;
  document.close(); // 回调不访问已经关闭的文档。
  if (test_support::await_file(*files, receipt).failed() ||
      document.accept_save(*receipt) != engine::error::invalid_state)
    return 8;
  files.reset();
  app.reset();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
