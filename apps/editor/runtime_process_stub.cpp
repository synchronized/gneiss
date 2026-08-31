// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

namespace gneiss::editor {

struct runtime_process::implementation final {
  std::string output;
};

runtime_process::runtime_process() : implementation_(std::make_unique<implementation>()) {}
runtime_process::~runtime_process() = default;

result runtime_process::start(const std::filesystem::path&,
                              const runtime_launch_request&) noexcept {
  return result::unsupported;
}

result runtime_process::request_stop() noexcept { return result::not_ready; }
void runtime_process::update() noexcept {}
bool runtime_process::is_running() const noexcept { return false; }
bool runtime_process::has_started() const noexcept { return false; }
int runtime_process::exit_code() const noexcept { return -1; }
const std::string& runtime_process::output() const noexcept { return implementation_->output; }
void runtime_process::clear_output() noexcept { implementation_->output.clear(); }

} // namespace gneiss::editor
