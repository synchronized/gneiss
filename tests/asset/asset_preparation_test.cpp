// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/asset_preparation.hpp"

#include "engine/asset/virtual_file_system.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

namespace {
// 准备契约不能再次退化为带纹理句柄与发布操作的资源结构。
template <typename T>
concept has_texture_handle = requires(T value) { value.base_color_texture; };
template <typename T>
concept has_publication_description = requires(T value) { value.description(); };
static_assert(!has_texture_handle<decltype(gneiss::asset_internal::prepared_asset::material)>);
static_assert(
    !has_publication_description<decltype(gneiss::asset_internal::prepared_asset::material)>);

template <typename T>
concept has_upload_reference = requires(T value) { value.upload_payload; };
static_assert(!has_upload_reference<decltype(gneiss::asset_internal::prepared_asset::texture)>);

class mesh_source final : public gneiss::asset_internal::file_system {
public:
  mutable unsigned reads{};
  bool change_on_verify{};
  gneiss_result read(std::string_view path,
                     std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, std::numeric_limits<std::size_t>::max(), output);
  }
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& output) const noexcept override {
    constexpr std::string_view mesh =
        R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
    constexpr std::string_view material =
        R"({"format":"gneiss.material","version":1,"color":[1,1,1,1]})";
    constexpr std::string_view linked_material =
        R"({"format":"gneiss.material","version":2,"color":[1,1,1,1],"base_color_texture":"asset://dependency"})";
    auto bytes = mesh;
    if (path == "material") {
      bytes = material;
    } else if (path == "linked") {
      bytes = linked_material;
    }
    output.clear();
    if (path != "mesh" && path != "material" && path != "linked") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (bytes.size() > limit) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      output.resize(bytes.size());
      std::memcpy(output.data(), bytes.data(), bytes.size());
      if (++reads > 1U && change_on_verify) {
        output.back() = std::byte{' '};
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};
struct range_data {
  std::vector<std::byte> bytes;
  std::size_t read_bytes{};
  bool fail{};
  bool deferred{};
  bool ready{};
  unsigned live_reads{};
  unsigned maximum_live_reads{};
};
class range_reader final : public gneiss::asset_internal::read_source {
public:
  explicit range_reader(std::shared_ptr<range_data> data) : data_(std::move(data)) {}
  [[nodiscard]] std::uint64_t size() const noexcept override { return data_->bytes.size(); }
  [[nodiscard]] gneiss_result begin_read(
      std::uint64_t offset, std::size_t count,
      std::unique_ptr<gneiss::asset_internal::read_operation>& output) const noexcept override {
    output.reset();
    if (!data_->deferred) {
      return GNEISS_ERROR_UNSUPPORTED;
    }
    class operation final : public gneiss::asset_internal::read_operation {
    public:
      operation(std::shared_ptr<range_data> data, std::span<const std::byte> bytes)
          : data_(std::move(data)), bytes_(bytes) {
        ++data_->live_reads;
        data_->maximum_live_reads = std::max(data_->maximum_live_reads, data_->live_reads);
      }
      ~operation() override { --data_->live_reads; }
      [[nodiscard]] gneiss_result poll(std::span<const std::byte>& output) noexcept override {
        output = {};
        if (!data_->ready) {
          return GNEISS_ERROR_NOT_READY;
        }
        if (data_->fail) {
          return GNEISS_ERROR_IO;
        }
        output = bytes_;
        if (!counted_) {
          data_->read_bytes += bytes_.size();
          counted_ = true;
        }
        return GNEISS_SUCCESS;
      }

    private:
      std::shared_ptr<range_data> data_;
      std::span<const std::byte> bytes_;
      bool counted_{};
    };
    if (offset > size() || count > size() - offset) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      output = std::make_unique<operation>(
          data_, std::span{data_->bytes}.subspan(static_cast<std::size_t>(offset), count));
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
  [[nodiscard]] gneiss_result read_at(std::uint64_t offset,
                                      std::span<std::byte> output) const noexcept override {
    if (data_->fail) {
      return GNEISS_ERROR_IO;
    }
    if (offset > data_->bytes.size() || output.size() > data_->bytes.size() - offset) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ranges::copy(
        std::span{data_->bytes}.subspan(static_cast<std::size_t>(offset), output.size()),
        output.begin());
    data_->read_bytes += output.size();
    return GNEISS_SUCCESS;
  }

private:
  std::shared_ptr<range_data> data_;
};
class range_files final : public gneiss::asset_internal::file_system {
public:
  std::shared_ptr<range_data> data = std::make_shared<range_data>();
  gneiss_result read(std::string_view /*path*/,
                     std::vector<std::byte>& /*output*/) const noexcept override {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  gneiss_result
  open_read(std::string_view /*path*/,
            std::unique_ptr<gneiss::asset_internal::read_source>& output) const noexcept override
      try {
    output = std::make_unique<range_reader>(data);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
};
struct preparation_faults {
  bool cancel{};
  bool change{};
  bool fail{};
};
bool run_preparation_case(const std::shared_ptr<range_files>& backend,
                          const gneiss::asset_internal::virtual_file_system& files,
                          std::span<const gneiss::asset_internal::asset_request> requests,
                          preparation_faults faults) {
  using namespace gneiss::asset_internal;
  const auto [cancel, change, fail] = faults;
  asset_preparation preparation(files, requests, {.maximum_assets = 1U, .maximum_bytes = 65536U},
                                {});
  prepared_batch output;
  asset_diagnostic diagnostic;
  bool complete{};
  backend->data->read_bytes = 0U;
  backend->data->fail = false;
  backend->data->bytes.back() = std::byte{' '};
  for (unsigned step = 0U; step < 128U; ++step) {
    const auto before = backend->data->read_bytes;
    backend->data->fail = fail && step == 2U;
    const auto result = preparation.advance(
        128U, [&] { return cancel && step == 2U; }, output, diagnostic, complete);
    if (backend->data->read_bytes - before > 128U) {
      return false;
    }
    if (change && backend->data->read_bytes == backend->data->bytes.size()) {
      backend->data->bytes.back() = std::byte{'\n'};
    }
    if (complete) {
      if (cancel || change || fail) {
        return result == (fail ? GNEISS_ERROR_IO : GNEISS_ERROR_INVALID_STATE) &&
               output.assets.empty();
      }
      return result == GNEISS_SUCCESS && output.assets.size() == 1U &&
             output.assets.front().mesh.vertices.size() == 3U;
    }
    if (result != GNEISS_SUCCESS || !output.assets.empty()) {
      return false;
    }
  }
  return false;
}
bool incremental_preparation() {
  using namespace gneiss::asset_internal;
  auto backend = std::make_shared<range_files>();
  mesh_source fixture;
  if (fixture.read("mesh", backend->data->bytes) != GNEISS_SUCCESS) {
    return false;
  }
  backend->data->bytes.resize(4096U, std::byte{' '});
  virtual_file_system files;
  if (files.mount("asset://", backend) != GNEISS_SUCCESS) {
    return false;
  }
  const std::array requests{asset_request{.uri = "asset://mesh", .type = asset_type::mesh}};
  return run_preparation_case(backend, files, requests, {}) &&
         run_preparation_case(backend, files, requests, {.cancel = true}) &&
         run_preparation_case(backend, files, requests, {.change = true}) &&
         run_preparation_case(backend, files, requests, {.fail = true}) &&
         run_preparation_case(backend, files, requests, {});
}

bool prefetched_preparation() {
  using namespace gneiss::asset_internal;
  auto backend = std::make_shared<range_files>();
  mesh_source fixture;
  if (fixture.read("mesh", backend->data->bytes) != GNEISS_SUCCESS) {
    return false;
  }
  backend->data->deferred = true;
  virtual_file_system files;
  if (files.mount("asset://", backend) != GNEISS_SUCCESS) {
    return false;
  }
  std::vector<asset_request> requested;
  requested.reserve(16U);
  for (unsigned i = 0U; i < 16U; ++i) {
    requested.push_back({.uri = "asset://mesh-" + std::to_string(i), .type = asset_type::mesh});
  }
  prepared_batch output;
  asset_diagnostic diagnostic;
  bool complete{};
  {
    asset_preparation preparation(files, requested,
                                  {.maximum_assets = 16U, .maximum_bytes = 65536U}, {});
    if (preparation.advance(65536U, {}, output, diagnostic, complete) != GNEISS_SUCCESS ||
        complete || backend->data->live_reads != 8U || !output.assets.empty()) {
      return false;
    }
    if (preparation.advance(
            65536U, [] { return true; }, output, diagnostic, complete) !=
            GNEISS_ERROR_INVALID_STATE ||
        !complete || backend->data->live_reads != 0U) {
      return false;
    }
  }
  // 取消后可用同一后端重试，所有候选仍须在复验成功后一次发布。
  backend->data->ready = true;
  asset_preparation preparation(files, requested, {.maximum_assets = 16U, .maximum_bytes = 65536U},
                                {});
  complete = false;
  for (unsigned step = 0U; step < 64U && !complete; ++step) {
    if (preparation.advance(65536U, {}, output, diagnostic, complete) != GNEISS_SUCCESS) {
      return false;
    }
  }
  return complete && output.assets.size() == requested.size() && backend->data->live_reads == 0U &&
         backend->data->maximum_live_reads <= 9U;
}
int verify_incremental_paths() {
  if (!incremental_preparation()) {
    return 12;
  }
  return prefetched_preparation() ? 0 : 13;
}
} // namespace

int main() try {
  const auto incremental_result = verify_incremental_paths();
  if (incremental_result != 0) {
    return incremental_result;
  }
  using namespace gneiss::asset_internal;
  prepared_batch output;
  asset_diagnostic diagnostic;
  gneiss_result result = GNEISS_ERROR_INTERNAL;
  // 不创建 Application、缓存或资源服务；后台输出在 VFS 销毁后仍拥有完整数组。
  std::thread worker([&] {
    try {
      gneiss::asset_internal::virtual_file_system files;
      auto source = std::make_shared<mesh_source>();
      if (files.mount("asset://", source) != GNEISS_SUCCESS) {
        return;
      }
      const std::array requests{
          asset_request{.uri = "asset://mesh", .type = asset_type::mesh},
      };
      result = prepare_assets(files, requests, output, diagnostic, {}, 0U);
    } catch (...) {
      result = GNEISS_ERROR_OUT_OF_MEMORY;
    }
  });
  worker.join();
  if (result != GNEISS_SUCCESS || output.assets.size() != 1U ||
      output.assets[0].mesh.vertices.size() != 3U || output.bytes == 0U) {
    return 1;
  }

  gneiss::asset_internal::virtual_file_system files;
  auto source = std::make_shared<mesh_source>();
  if (files.mount("asset://", source) != GNEISS_SUCCESS) {
    return 2;
  }
  const std::array requests{
      asset_request{.uri = "asset://mesh", .type = asset_type::mesh},
  };
  if (prepare_assets(
          files, requests, output, diagnostic, [] { return true; }, 0U) !=
          GNEISS_ERROR_INVALID_STATE ||
      !output.assets.empty() || output.bytes != 0U) {
    return 3;
  }
  if (prepare_assets(files, requests, output, diagnostic, {}, 0U, 1U, 1U) == GNEISS_SUCCESS ||
      !output.assets.empty()) {
    return 4;
  }
  source->change_on_verify = true;
  if (prepare_assets(files, requests, output, diagnostic, {}, 0U) != GNEISS_ERROR_INVALID_STATE ||
      !output.assets.empty() || diagnostic.result != GNEISS_ERROR_INVALID_STATE) {
    return 5;
  }
  const std::array missing{
      asset_request{.uri = "asset://missing", .type = asset_type::mesh},
  };
  if (prepare_assets(files, missing, output, diagnostic, {}, 0U) != GNEISS_ERROR_NOT_FOUND ||
      !output.assets.empty()) {
    return 6;
  }
  source->change_on_verify = false;
  const std::array material_request{
      asset_request{.uri = "asset://material", .type = asset_type::material},
  };
  if (prepare_assets(files, material_request, output, diagnostic, {}, 1000U, 1U, 1000U) !=
          GNEISS_SUCCESS ||
      output.bytes != 1000U || output.assets.size() != 1U) {
    return 8;
  }
  if (prepare_assets(files, material_request, output, diagnostic, {}, 0U) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      !output.assets.empty()) {
    return 9;
  }
  if (prepare_assets(files, material_request, output, diagnostic, {}, 1001U, 1U, 1000U) !=
          GNEISS_ERROR_OUT_OF_MEMORY ||
      !output.assets.empty()) {
    return 10;
  }
  const std::array linked_request{
      asset_request{.uri = "asset://linked", .type = asset_type::material},
  };
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  if (prepare_assets(files, linked_request, output, diagnostic, {}, maximum, 2U, maximum) !=
          GNEISS_ERROR_OUT_OF_MEMORY ||
      !output.assets.empty()) {
    return 11;
  }
  return 0;
} catch (...) {
  return 7;
}
