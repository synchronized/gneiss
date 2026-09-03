// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_transaction.h"

#include <string>
#include <vector>

namespace {

using gneiss::editor::author_document;
using gneiss::editor::author_document_change;
using gneiss::editor::author_transaction;

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
  std::vector documents{
      author_document{.path = "scenes/main.scene.json", .contents = "scene-v1"}};
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
  return transaction.add({.path = "", .baseline = std::string{"a"},
                          .replacement = std::string{"b"}}) ==
             gneiss::result::invalid_argument &&
         transaction.add({.path = "same", .baseline = std::string{"a"},
                          .replacement = std::string{"a"}}) ==
             gneiss::result::invalid_argument &&
         transaction.add({.path = "one", .baseline = std::string{"a"},
                          .replacement = std::string{"b"}}) == gneiss::result::success &&
         transaction.add({.path = "one", .baseline = std::string{"a"},
                          .replacement = std::string{"c"}}) ==
             gneiss::result::invalid_argument;
}

} // namespace

int main() try {
  return test_commit_undo_redo() && test_conflict_is_atomic() &&
                 test_failure_rolls_back_and_can_retry() && test_create_and_external_edit_guard() &&
                 test_invalid_changes()
             ? 0
             : 1;
} catch (...) {
  return 99;
}
