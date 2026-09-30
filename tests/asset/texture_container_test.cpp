// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/texture_container.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss::asset_internal;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("容器读取契约失败，行=" + std::to_string(where.line()));
  }
}

struct observation {
  std::size_t bytes{};
  std::size_t calls{};
  std::size_t fail_call{};
  bool destroyed{};
  bool changed{};
};

class counted_source final : public read_source {
public:
  counted_source(std::vector<std::byte> bytes, std::shared_ptr<observation> counts)
      : bytes_(std::move(bytes)), counts_(std::move(counts)) {}
  ~counted_source() override { counts_->destroyed = true; }
  [[nodiscard]] std::uint64_t size() const noexcept override { return bytes_.size(); }
  [[nodiscard]] gneiss_result read_at(std::uint64_t offset,
                                      std::span<std::byte> output) const noexcept override {
    ++counts_->calls;
    if (counts_->fail_call == counts_->calls) {
      return GNEISS_ERROR_IO;
    }
    if (offset > bytes_.size() || output.size() > bytes_.size() - offset) {
      return GNEISS_ERROR_IO;
    }
    counts_->bytes += output.size();
    std::ranges::copy(std::span{bytes_}.subspan(static_cast<std::size_t>(offset), output.size()),
                      output.begin());
    if (counts_->changed && !output.empty())
      output.back() ^= std::byte{1};
    return GNEISS_SUCCESS;
  }

private:
  std::vector<std::byte> bytes_;
  std::shared_ptr<observation> counts_;
};

void run() {
  const std::array manifest = {std::byte{1}, std::byte{2}, std::byte{3}};
  std::vector<std::byte> payload(std::size_t{1024U} * 1024U, std::byte{7});
  std::vector<std::byte> encoded;
  std::string diagnostic;
  check(encode_texture_binary(manifest, payload, encoded, diagnostic) ==
        texture_binary_result::success);
  texture_container container;
  check(container.read_payload(0U, {}) == GNEISS_ERROR_INVALID_STATE);
  auto counts = std::make_shared<observation>();
  check(container.open(std::make_unique<counted_source>(encoded, counts), 3U, diagnostic) ==
        GNEISS_SUCCESS);
  check(counts->bytes == 80U && !counts->destroyed);
  check(std::ranges::equal(container.manifest(), manifest));
  check(container.payload_size() == payload.size());
  std::array<std::byte, 4> chunk{};
  check(container.read_payload(4096U, chunk) == GNEISS_SUCCESS);
  check(counts->bytes == 84U && chunk[0] == std::byte{7} && chunk[3] == std::byte{7});
  const auto calls = counts->calls;
  check(container.read_payload(payload.size() - 1U, chunk) == GNEISS_ERROR_INVALID_ARGUMENT);
  check(container.read_payload(std::numeric_limits<std::uint64_t>::max(), chunk) ==
        GNEISS_ERROR_INVALID_ARGUMENT);
  check(counts->calls == calls);
  check(container.read_payload(payload.size(), {}) == GNEISS_SUCCESS);
  counts->fail_call = counts->calls + 1U;
  check(container.read_payload(0U, chunk) == GNEISS_ERROR_IO);
  check(container.read_payload(0U, chunk) == GNEISS_SUCCESS);

  auto limited = std::make_shared<observation>();
  check(container.open(std::make_unique<counted_source>(encoded, limited), 2U, diagnostic) ==
        GNEISS_ERROR_OUT_OF_MEMORY);
  check(counts->destroyed && limited->destroyed && limited->bytes == 64U);
  check(container.manifest().empty() && container.payload_size() == 0U);
  check(container.read_payload(0U, chunk) == GNEISS_ERROR_INVALID_STATE);
  // 头、Manifest、填充分别注入读取失败，均不保留部分状态。
  for (std::size_t call = 1U; call <= 3U; ++call) {
    auto failed = std::make_shared<observation>();
    failed->fail_call = call;
    check(container.open(std::make_unique<counted_source>(encoded, failed), 3U, diagnostic) ==
          GNEISS_ERROR_IO);
    check(failed->destroyed && container.manifest().empty());
  }
  for (const auto offset : {8U, 16U, 24U, 32U, 48U, 56U, 67U}) {
    auto bad = encoded;
    bad[offset] = std::byte{255};
    auto failed = std::make_shared<observation>();
    const auto status =
        container.open(std::make_unique<counted_source>(bad, failed), 3U, diagnostic);
    check(status == (offset == 8U ? GNEISS_ERROR_UNSUPPORTED : GNEISS_ERROR_INVALID_ARGUMENT));
    check(container.manifest().empty() && failed->destroyed);
  }
  auto truncated = encoded;
  truncated.resize(63U);
  check(container.open(std::make_unique<counted_source>(truncated, std::make_shared<observation>()),
                       3U, diagnostic) == GNEISS_ERROR_INVALID_ARGUMENT);
  check(container.open(nullptr, 3U, diagnostic) == GNEISS_ERROR_INVALID_ARGUMENT);
  truncated = encoded;
  truncated.pop_back();
  check(container.open(std::make_unique<counted_source>(truncated, std::make_shared<observation>()),
                       3U, diagnostic) == GNEISS_ERROR_INVALID_ARGUMENT);
  // 极大偏移或尺寸不能在头解析时回绕；新旧路径使用同一个校验器。
  for (const auto offset : {16U, 24U, 32U, 40U}) {
    auto overflow = encoded;
    std::fill_n(overflow.begin() + offset, 8U, std::byte{255});
    texture_binary_layout layout;
    check(decode_texture_binary_header(std::span{overflow}.first(64U), overflow.size(), layout,
                                       diagnostic) == texture_binary_result::invalid_container);
    texture_binary_view view;
    check(decode_texture_binary(overflow, view, diagnostic) ==
          texture_binary_result::invalid_container);
  }
  const std::array<std::byte, 16> aligned_manifest{};
  check(encode_texture_binary(aligned_manifest, payload, encoded, diagnostic) ==
        texture_binary_result::success);
  auto aligned = std::make_shared<observation>();
  {
    texture_container aligned_container;
    check(aligned_container.open(std::make_unique<counted_source>(encoded, aligned), 16U,
                                 diagnostic) == GNEISS_SUCCESS);
    check(aligned->calls == 2U && aligned->bytes == 80U);
  }
  check(aligned->destroyed);
}
void recovery() {
  const std::array manifest{std::byte{1}};
  const std::array payload{std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
  std::vector<std::byte> encoded;
  std::string diagnostic;
  check(encode_texture_binary(manifest, payload, encoded, diagnostic) ==
        texture_binary_result::success);
  auto counts = std::make_shared<observation>();
  auto container = std::make_shared<texture_container>();
  check(container->open(std::make_unique<counted_source>(encoded, counts), manifest.size(),
                        diagnostic) == GNEISS_SUCCESS);
  const auto selected = std::span{payload}.subspan(1U, 2U);
  auto source = std::make_unique<texture_payload_source>(container, 1U, selected.size(),
                                                         gneiss::core::sha256(selected));
  container.reset();
  check(!counts->destroyed);
  std::vector<std::byte> output{std::byte{99}};
  const auto calls = counts->calls;
  check(source->read(output, 1U) == GNEISS_ERROR_OUT_OF_MEMORY && output.empty());
  check(counts->calls == calls);
  check(source->read(output, 2U) == GNEISS_SUCCESS && std::ranges::equal(output, selected));
  counts->changed = true;
  check(source->read(output, 2U) == GNEISS_ERROR_INVALID_STATE && output.empty());
  counts->changed = false;
  counts->fail_call = counts->calls + 1U;
  check(source->read(output, 2U) == GNEISS_ERROR_IO && output.empty());
  check(source->read(output, 2U) == GNEISS_SUCCESS && std::ranges::equal(output, selected));
  source.reset();
  check(counts->destroyed);
  texture_payload_source invalid(nullptr, UINT64_MAX, UINT64_MAX, {});
  check(invalid.read(output, SIZE_MAX) == GNEISS_ERROR_INVALID_ARGUMENT && output.empty());
}
} // namespace

int main() try {
  run();
  recovery();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
