// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_transaction.h"
#include "native_author_transaction.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

using gneiss::editor::author_document;
using gneiss::editor::author_document_change;
using gneiss::editor::author_transaction;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] bool is(std::vector<author_document>& documents, std::string_view path,
                      const std::optional<std::string>& expected) {
  for (const auto& document : documents) {
    if (document.path == path) {
      return document.contents == expected;
    }
  }
  return !expected;
}

[[nodiscard]] author_transaction make_transaction() {
  author_transaction transaction;
  (void)transaction.add({.path = "prefabs/lamp.prefab.json",
                         .baseline = std::string{"prefab-v1"},
                         .replacement = std::string{"prefab-v2"}});
  (void)transaction.add({.path = "scenes/main.scene.json",
                         .baseline = std::string{"scene-with-overrides"},
                         .replacement = std::string{"scene-without-overrides"}});
  return transaction;
}

[[nodiscard]] bool test_commit_undo_redo() {
  std::vector documents{
      author_document{.path = "prefabs/lamp.prefab.json", .contents = "prefab-v1"},
      author_document{.path = "scenes/main.scene.json", .contents = "scene-with-overrides"}};
  auto transaction = make_transaction();
  const auto revision = transaction.baseline_revision(0U);
  return revision.byte_size == 9U && revision.digest != 0U &&
         transaction.prepare(documents) == gneiss::result::success &&
         transaction.commit(documents) == gneiss::result::success &&
         is(documents, "prefabs/lamp.prefab.json", "prefab-v2") &&
         is(documents, "scenes/main.scene.json", "scene-without-overrides") &&
         transaction.undo(documents) == gneiss::result::success &&
         is(documents, "prefabs/lamp.prefab.json", "prefab-v1") &&
         transaction.redo(documents) == gneiss::result::success &&
         is(documents, "scenes/main.scene.json", "scene-without-overrides");
}

[[nodiscard]] bool test_conflict_is_atomic() {
  std::vector documents{
      author_document{.path = "prefabs/lamp.prefab.json", .contents = "external-edit"},
      author_document{.path = "scenes/main.scene.json", .contents = "scene-with-overrides"}};
  auto transaction = make_transaction();
  return transaction.prepare(documents) == gneiss::result::invalid_state &&
         is(documents, "prefabs/lamp.prefab.json", "external-edit") &&
         is(documents, "scenes/main.scene.json", "scene-with-overrides");
}

[[nodiscard]] bool test_failure_rolls_back_and_can_retry() {
  std::vector documents{
      author_document{.path = "prefabs/lamp.prefab.json", .contents = "prefab-v1"},
      author_document{.path = "scenes/main.scene.json", .contents = "scene-with-overrides"}};
  auto transaction = make_transaction();
  return transaction.prepare(documents) == gneiss::result::success &&
         transaction.commit(documents, 1U) == gneiss::result::io &&
         is(documents, "prefabs/lamp.prefab.json", "prefab-v1") &&
         is(documents, "scenes/main.scene.json", "scene-with-overrides") &&
         transaction.commit(documents) == gneiss::result::success;
}

[[nodiscard]] bool test_create_and_external_edit_guard() {
  std::vector documents{author_document{.path = "scenes/main.scene.json", .contents = "scene-v1"}};
  author_transaction transaction;
  if (transaction.add({.path = "prefabs/new.prefab.json",
                       .baseline = std::nullopt,
                       .replacement = std::string{"new-prefab"}}) != gneiss::result::success ||
      transaction.add({.path = "scenes/main.scene.json",
                       .baseline = std::string{"scene-v1"},
                       .replacement = std::string{"scene-v2"}}) != gneiss::result::success ||
      transaction.prepare(documents) != gneiss::result::success ||
      transaction.commit(documents) != gneiss::result::success ||
      !is(documents, "prefabs/new.prefab.json", "new-prefab")) {
    return false;
  }
  documents[0].contents = "external-scene-edit";
  return transaction.undo(documents) == gneiss::result::invalid_state &&
         is(documents, "prefabs/new.prefab.json", "new-prefab");
}

[[nodiscard]] bool test_invalid_changes() {
  author_transaction transaction;
  return transaction.add(
             {.path = "", .baseline = std::string{"a"}, .replacement = std::string{"b"}}) ==
             gneiss::result::invalid_argument &&
         transaction.add(
             {.path = "same", .baseline = std::string{"a"}, .replacement = std::string{"a"}}) ==
             gneiss::result::invalid_argument &&
         transaction.add(
             {.path = "one", .baseline = std::string{"a"}, .replacement = std::string{"b"}}) ==
             gneiss::result::success &&
         transaction.add(
             {.path = "one", .baseline = std::string{"a"}, .replacement = std::string{"c"}}) ==
             gneiss::result::invalid_argument;
}

[[nodiscard]] bool test_native_commit_and_conflict(const std::filesystem::path& root) {
  const auto prefab = root / "prefabs" / "lamp.prefab.json";
  const auto scene = root / "scenes" / "main.scene.json";
  if (!write_file(prefab, "prefab-v1") || !write_file(scene, "scene-v1")) {
    return false;
  }
  const std::vector changes{author_document_change{.path = "prefabs/lamp.prefab.json",
                                                   .baseline = std::string{"prefab-v1"},
                                                   .replacement = std::string{"prefab-v2"}},
                            author_document_change{.path = "scenes/main.scene.json",
                                                   .baseline = std::string{"scene-v1"},
                                                   .replacement = std::string{"scene-v2"}}};
  if (gneiss::editor::commit_native_author_transaction(root, changes) != gneiss::result::success ||
      read_file(prefab) != "prefab-v2" || read_file(scene) != "scene-v2") {
    return false;
  }
  const std::vector conflict{author_document_change{.path = "prefabs/lamp.prefab.json",
                                                    .baseline = std::string{"prefab-v1"},
                                                    .replacement = std::string{"prefab-v3"}}};
  return gneiss::editor::commit_native_author_transaction(root, conflict) ==
             gneiss::result::invalid_state &&
         read_file(prefab) == "prefab-v2";
}

[[nodiscard]] bool test_native_recovery(const std::filesystem::path& root) {
  const auto prefab = root / "prefabs" / "lamp.prefab.json";
  const auto scene = root / "scenes" / "main.scene.json";
  const std::vector changes{author_document_change{.path = "prefabs/lamp.prefab.json",
                                                   .baseline = std::string{"prefab-v2"},
                                                   .replacement = std::string{"prefab-v3"}},
                            author_document_change{.path = "scenes/main.scene.json",
                                                   .baseline = std::string{"scene-v2"},
                                                   .replacement = std::string{"scene-v3"}}};
  gneiss::editor::native_author_transaction_options interruption;
  interruption.interrupt_after_replacements = 1U;
  if (gneiss::editor::commit_native_author_transaction(root, changes, interruption) !=
          gneiss::result::not_ready ||
      read_file(prefab) != "prefab-v3" || read_file(scene) != "scene-v2" ||
      gneiss::editor::recover_native_author_transactions(root) != gneiss::result::success ||
      read_file(prefab) != "prefab-v2" || read_file(scene) != "scene-v2") {
    return false;
  }
  interruption.interrupt_after_replacements = std::numeric_limits<std::size_t>::max();
  interruption.interrupt_after_commit_marker = true;
  return gneiss::editor::commit_native_author_transaction(root, changes, interruption) ==
             gneiss::result::not_ready &&
         gneiss::editor::recover_native_author_transactions(root) == gneiss::result::success &&
         read_file(prefab) == "prefab-v3" && read_file(scene) == "scene-v3";
}

[[nodiscard]] bool test_native_create_and_path_guard(const std::filesystem::path& root) {
  const std::vector create{author_document_change{.path = "prefabs/new.prefab.json",
                                                  .baseline = std::nullopt,
                                                  .replacement = std::string{"created"}}};
  if (gneiss::editor::commit_native_author_transaction(root, create) != gneiss::result::success ||
      read_file(root / "prefabs" / "new.prefab.json") != "created") {
    return false;
  }
  const std::vector escaped{author_document_change{
      .path = "../escaped.json", .baseline = std::nullopt, .replacement = std::string{"escaped"}}};
  return gneiss::editor::commit_native_author_transaction(root, escaped) ==
             gneiss::result::invalid_argument &&
         !std::filesystem::exists(root.parent_path() / "escaped.json");
}

} // namespace

int main() try {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-author-transaction-test-" + std::to_string(suffix));
  std::filesystem::create_directories(root / "prefabs");
  std::filesystem::create_directories(root / "scenes");
  const auto first = test_commit_undo_redo() && test_conflict_is_atomic() &&
                     test_failure_rolls_back_and_can_retry() &&
                     test_create_and_external_edit_guard() && test_invalid_changes();
  const auto second = test_native_commit_and_conflict(root);
  const auto third = test_native_recovery(root);
  const auto fourth = test_native_create_and_path_guard(root);
  const auto passed = first && second && third && fourth;
  std::filesystem::remove_all(root);
  return passed ? 0 : 1;
} catch (...) {
  return 99;
}
