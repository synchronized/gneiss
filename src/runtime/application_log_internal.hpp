// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_LOG_INTERNAL_HPP_
#define GNEISS_SRC_APPLICATION_APPLICATION_LOG_INTERNAL_HPP_

#include <gneiss/engine/application.h>

#include <string_view>

namespace gneiss::log_internal {
struct message_view;
}

namespace gneiss::application_internal {

[[nodiscard]] gneiss_result submit_application_log(gneiss_application application,
                                                   const log_internal::message_view& message,
                                                   std::string_view source) noexcept;

} // namespace gneiss::application_internal

#endif
