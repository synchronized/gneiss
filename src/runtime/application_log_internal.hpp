// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_LOG_INTERNAL_HPP_
#define GNEISS_SRC_APPLICATION_APPLICATION_LOG_INTERNAL_HPP_

#include "engine/core/result.hpp"
#include <gneiss/engine/application.h>

#include <string_view>

namespace gneiss::engine {
struct message_view;
}

namespace gneiss::runtime {

[[nodiscard]] gneiss::engine::result
submit_application_log(gneiss_application application, const gneiss::engine::message_view& message,
                       std::string_view source) noexcept;

} // namespace gneiss::runtime

#endif
