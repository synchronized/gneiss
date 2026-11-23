// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/commands/author_file_save.hpp"
#include <span>
namespace gneiss::editor {
gneiss::error begin_author_file_save(gneiss::files::service& files,
                                     author_document_change change,
                                     std::shared_ptr<author_file_save>& output) {
  output.reset();
  if (change.path.empty() || !change.replacement)
    return gneiss::error::invalid_argument;
  {
    auto receipt = std::make_shared<author_file_save>();
    auto write = [&files, receipt, uri = "asset://" + change.path, content = *change.replacement,
                  exists = change.baseline.has_value()]() {
      {
        const auto bytes = std::as_bytes(std::span{content.data(), content.size()});
        gneiss::files::request_id request;
        auto status = files.write(
            uri, {bytes.begin(), bytes.end()},
            {exists ? gneiss::files::write_mode::replace
                    : gneiss::files::write_mode::create_new},
            [receipt](gneiss::error value) {
              receipt->status = value;
              receipt->complete = true;
            },
            request);
        if (status.failed()) {
          receipt->status = status;
          receipt->complete = true;
        }
        return status;
      }
    };
    gneiss::error status;
    if (change.baseline) {
      gneiss::files::request_id request;
      status = files.read(
          "asset://" + change.path, {16U * 1024U * 1024U},
          [receipt, baseline = std::move(*change.baseline), write = std::move(write)](
              gneiss::files::completion<gneiss::files::bytes> completion) mutable {
            if (completion.status.ok() &&
                std::string_view(reinterpret_cast<const char*>(completion.data.data()),
                                 completion.data.size()) != baseline)
              completion.status = gneiss::error::invalid_state;
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
  }
}
}
