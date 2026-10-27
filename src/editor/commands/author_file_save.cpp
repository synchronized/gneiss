// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/author_file_save.hpp"
#include <span>
namespace gneiss::editor {
engine::result begin_author_file_save(gneiss::engine::files::service& files,
                                      author_document_change change,
                                      std::shared_ptr<author_file_save>& output) noexcept {
  output.reset();
  if (change.path.empty() || !change.replacement)
    return engine::result::invalid_argument;
  try {
    auto receipt = std::make_shared<author_file_save>();
    auto write = [&files, receipt, uri = "asset://" + change.path, content = *change.replacement,
                  exists = change.baseline.has_value()]() {
      try {
        const auto bytes = std::as_bytes(std::span{content.data(), content.size()});
        gneiss::engine::files::request_id request;
        auto status = files.write(
            uri, {bytes.begin(), bytes.end()},
            {exists ? gneiss::engine::files::write_mode::replace
                    : gneiss::engine::files::write_mode::create_new},
            [receipt](engine::result value) {
              receipt->status = value;
              receipt->complete = true;
            },
            request);
        if (status.failed()) {
          receipt->status = status;
          receipt->complete = true;
        }
        return status;
      } catch (...) {
        receipt->status = engine::result::out_of_memory;
        receipt->complete = true;
        return receipt->status;
      }
    };
    engine::result status;
    if (change.baseline) {
      gneiss::engine::files::request_id request;
      status = files.read(
          "asset://" + change.path, {16U * 1024U * 1024U},
          [receipt, baseline = std::move(*change.baseline), write = std::move(write)](
              gneiss::engine::files::completion<gneiss::engine::files::bytes> completion) mutable {
            if (completion.status.ok() &&
                std::string_view(reinterpret_cast<const char*>(completion.data.data()),
                                 completion.data.size()) != baseline)
              completion.status = engine::result::invalid_state;
            if (completion.status.failed()) {
              receipt->status = completion.status;
              receipt->complete = true;
              return;
            }
            (void)write();
          },
          request);
    } else
      status = write();
    if (status.ok())
      output = std::move(receipt);
    return status;
  } catch (const std::bad_alloc&) {
    return engine::result::out_of_memory;
  } catch (...) {
    return engine::result::internal;
  }
}
}
