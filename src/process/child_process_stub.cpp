// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "child_process.h"

namespace gneiss {

struct child_process::implementation final {
  std::string output;
  std::string pending_output;
};

child_process::child_process() : implementation_(std::make_unique<implementation>()) {}
child_process::~child_process() = default;

result child_process::start(const child_process_start_info&) noexcept {
  return result::unsupported;
}
result child_process::terminate() noexcept { return result::not_ready; }
void child_process::update() noexcept {}
bool child_process::is_running() const noexcept { return false; }
bool child_process::has_started() const noexcept { return false; }
int child_process::exit_code() const noexcept { return -1; }
const std::string& child_process::output() const noexcept { return implementation_->output; }
void child_process::consume_output(std::string& output) noexcept {
  output.clear();
  output.swap(implementation_->pending_output);
}
void child_process::clear_output() noexcept {
  implementation_->output.clear();
  implementation_->pending_output.clear();
}
void child_process::append_output(std::string_view text) noexcept {
  try {
    implementation_->output.append(text);
    implementation_->pending_output.append(text);
  } catch (...) {
  }
}

} // namespace gneiss
