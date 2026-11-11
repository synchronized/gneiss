// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/default_service.hpp"
namespace gneiss::test_support {
class log_capture {
  class output final : public engine::logging::sink {
  public:
    explicit output(engine::logging::record_callback callback) : callback_(std::move(callback)) {}
    engine::error write(const engine::logging::record_view& value) override {
      callback_(value);
      return engine::error::success;
    }
    engine::error flush() override { return engine::error::success; }

  private:
    engine::logging::record_callback callback_;
  };

public:
  explicit log_capture(engine::logging::record_callback callback) : binding(service) {
    service.attach(std::make_unique<output>(std::move(callback)));
  }
  engine::logging::service service;
  engine::logging::default_service_binding binding;
};
}
