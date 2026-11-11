// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/default_service.hpp"
namespace gneiss::test_support {
class log_capture {
  class output final : public gneiss::logging::sink {
  public:
    explicit output(gneiss::logging::record_callback callback) : callback_(std::move(callback)) {}
    gneiss::error write(const gneiss::logging::record_view& value) override {
      callback_(value);
      return gneiss::error::success;
    }
    gneiss::error flush() override { return gneiss::error::success; }

  private:
    gneiss::logging::record_callback callback_;
  };

public:
  explicit log_capture(gneiss::logging::record_callback callback) : binding(service) {
    service.attach(std::make_unique<output>(std::move(callback)));
  }
  gneiss::logging::service service;
  gneiss::logging::default_service_binding binding;
};
}
