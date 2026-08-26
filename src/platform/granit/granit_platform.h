// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_H_
#define GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_H_

#include <gneiss/application.h>

#include <granit/window.hpp>

namespace gneiss::application_internal {

class granit_platform final {
public:
  [[nodiscard]] gneiss_result initialize(const gneiss_application_desc& desc) noexcept;
  [[nodiscard]] gneiss_result poll(bool& out_should_close) noexcept;

private:
  granit::window_system window_system_;
  granit::window window_;
};

} // namespace gneiss::application_internal

#endif
