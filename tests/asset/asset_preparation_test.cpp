// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/resource/loaders/asset_preparation.hpp"
#include "engine/resource/loaders/material_description_decoder.hpp"

#include "engine/resource/vfs/virtual_file_system.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

namespace {
gneiss::engine::result
prepare_test_materials(const gneiss::engine::virtual_file_system& files,
                       std::span<const gneiss::engine::asset_preparation_source> requested,
                       gneiss::engine::prepared_batch& output,
                       gneiss::engine::asset_diagnostic& diagnostic,
                       const std::function<bool()>& cancelled, std::size_t material_bytes,
                       std::size_t count = 256U, std::size_t limit = 256U * 1024U * 1024U) {
  using namespace gneiss::engine;
  material_descriptions descriptions;
  for (const auto& source : requested) {
    std::vector<std::byte> bytes;
    auto description = std::make_shared<material_description>();
    auto status = files.read(source.uri, bytes);
    if (status == result::success)
      status = decode_material_description(bytes, *description, diagnostic);
    if (status.failed())
      return status;
    descriptions.emplace(source.uri, asset_ref<const material_description>{description});
  }
  return prepare_assets(files, requested, output, diagnostic, cancelled, material_bytes, count,
                        limit, {}, std::move(descriptions));
}

// 准备契约不能再次退化为带纹理句柄与发布操作的资源结构。
template <typename T>
concept has_texture_handle = requires(T value) { value.base_color_texture; };
template <typename T>
concept has_publication_description = requires(T value) { value.description(); };
static_assert(!has_texture_handle<decltype(gneiss::engine::prepared_asset::material)>);
static_assert(!has_publication_description<decltype(gneiss::engine::prepared_asset::material)>);

template <typename T>
concept has_upload_reference = requires(T value) { value.upload_payload; };
static_assert(!has_upload_reference<decltype(gneiss::engine::prepared_asset::texture)>);

class mesh_source final : public gneiss::engine::file_system {
public:
  mutable unsigned reads{};
  bool change_on_verify{};
  gneiss::engine::result read(std::string_view path,
                              std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, std::numeric_limits<std::size_t>::max(), output);
  }
  gneiss::engine::result read_bounded(std::string_view path, std::size_t limit,
                                      std::vector<std::byte>& output) const noexcept override {
    constexpr std::string_view mesh =
        R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
    constexpr std::string_view material =
        R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[1,1,1,1]})";
    constexpr std::string_view linked_material =
        R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://dependency"})";
    auto bytes = mesh;
    if (path == "material") {
      bytes = material;
    } else if (path == "linked") {
      bytes = linked_material;
    }
    output.clear();
    if (path != "mesh" && path != "material" && path != "linked") {
      return gneiss::engine::result::not_found;
    }
    if (bytes.size() > limit) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      output.resize(bytes.size());
      std::memcpy(output.data(), bytes.data(), bytes.size());
      if (++reads > 1U && change_on_verify) {
        output.back() = std::byte{' '};
      }
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
};
struct range_data {
  std::vector<std::byte> bytes;
  std::size_t read_bytes{};
  bool fail{};
  bool deferred{};
  bool deferred_open{};
  unsigned live_opens{};
  unsigned synchronous_opens{};
  bool ready{};
  unsigned live_reads{};
  unsigned maximum_live_reads{};
};
class range_reader final : public gneiss::engine::read_source {
public:
  explicit range_reader(std::shared_ptr<range_data> data) : data_(std::move(data)) {}
  [[nodiscard]] std::uint64_t size() const noexcept override { return data_->bytes.size(); }
  [[nodiscard]] gneiss::engine::result
  begin_read(std::uint64_t offset, std::size_t count,
             std::unique_ptr<gneiss::engine::read_operation>& output) const noexcept override {
    output.reset();
    if (!data_->deferred) {
      return gneiss::engine::result::unsupported;
    }
    class operation final : public gneiss::engine::read_operation {
    public:
      operation(std::shared_ptr<range_data> data, std::span<const std::byte> bytes)
          : data_(std::move(data)), bytes_(bytes) {
        ++data_->live_reads;
        data_->maximum_live_reads = std::max(data_->maximum_live_reads, data_->live_reads);
      }
      ~operation() override { --data_->live_reads; }
      [[nodiscard]] gneiss::engine::result
      poll(std::span<const std::byte>& output) noexcept override {
        output = {};
        if (!data_->ready) {
          return gneiss::engine::result::not_ready;
        }
        if (data_->fail) {
          return gneiss::engine::result::io;
        }
        output = bytes_;
        if (!counted_) {
          data_->read_bytes += bytes_.size();
          counted_ = true;
        }
        return gneiss::engine::result::success;
      }

    private:
      std::shared_ptr<range_data> data_;
      std::span<const std::byte> bytes_;
      bool counted_{};
    };
    if (offset > size() || count > size() - offset) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      output = std::make_unique<operation>(
          data_, std::span{data_->bytes}.subspan(static_cast<std::size_t>(offset), count));
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  [[nodiscard]] gneiss::engine::result
  read_at(std::uint64_t offset, std::span<std::byte> output) const noexcept override {
    if (data_->fail) {
      return gneiss::engine::result::io;
    }
    if (offset > data_->bytes.size() || output.size() > data_->bytes.size() - offset) {
      return gneiss::engine::result::invalid_argument;
    }
    std::ranges::copy(
        std::span{data_->bytes}.subspan(static_cast<std::size_t>(offset), output.size()),
        output.begin());
    data_->read_bytes += output.size();
    return gneiss::engine::result::success;
  }

private:
  std::shared_ptr<range_data> data_;
};
class range_files final : public gneiss::engine::file_system {
public:
  std::shared_ptr<range_data> data = std::make_shared<range_data>();
  gneiss::engine::result read(std::string_view /*path*/,
                              std::vector<std::byte>& /*output*/) const noexcept override {
    return gneiss::engine::result::unsupported;
  }
  gneiss::engine::result begin_open_read(
      std::string_view /*path*/,
      std::unique_ptr<gneiss::engine::open_read_operation>& output) const noexcept override {
    output.reset();
    if (!data->deferred_open) {
      return gneiss::engine::result::unsupported;
    }
    class operation final : public gneiss::engine::open_read_operation {
    public:
      explicit operation(std::shared_ptr<range_data> data) : data_(std::move(data)) {
        ++data_->live_opens;
      }
      ~operation() override { --data_->live_opens; }
      gneiss::engine::result
      poll(std::unique_ptr<gneiss::engine::read_source>& output) noexcept override {
        output.reset();
        if (!data_->ready) {
          return gneiss::engine::result::not_ready;
        }
        if (data_->fail) {
          return gneiss::engine::result::io;
        }
        try {
          output = std::make_unique<range_reader>(data_);
          return gneiss::engine::result::success;
        } catch (...) {
          return gneiss::engine::result::out_of_memory;
        }
      }

    private:
      std::shared_ptr<range_data> data_;
    };
    try {
      output = std::make_unique<operation>(data);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  gneiss::engine::result
  open_read(std::string_view /*path*/,
            std::unique_ptr<gneiss::engine::read_source>& output) const noexcept override try {
    ++data->synchronous_opens;
    output = std::make_unique<range_reader>(data);
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::out_of_memory;
  }
};
struct preparation_faults {
  bool cancel{};
  bool change{};
  bool fail{};
};
bool run_preparation_case(const std::shared_ptr<range_files>& backend,
                          const gneiss::engine::virtual_file_system& files,
                          std::span<const gneiss::engine::asset_preparation_source> requests,
                          preparation_faults faults) {
  using namespace gneiss::engine;
  const auto [cancel, change, fail] = faults;
  asset_preparation preparation(files, requests, {.maximum_assets = 1U, .maximum_bytes = 65536U},
                                {});
  prepared_batch output;
  asset_diagnostic diagnostic;
  bool complete{};
  backend->data->read_bytes = 0U;
  backend->data->fail = false;
  backend->data->bytes.back() = std::byte{' '};
  const auto expected_digest = gneiss::engine::sha256(backend->data->bytes);
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
      if (cancel || fail) {
        return result ==
                   (fail ? gneiss::engine::result::io : gneiss::engine::result::invalid_state) &&
               output.assets.empty();
      }
      return result == gneiss::engine::result::success && output.assets.size() == 1U &&
             output.assets.front().mesh.vertices.size() == 3U &&
             output.assets.front().sources.size() == 1U &&
             output.assets.front().sources.front().digest == expected_digest &&
             backend->data->read_bytes == backend->data->bytes.size();
    }
    if (result != gneiss::engine::result::success || !output.assets.empty()) {
      return false;
    }
  }
  return false;
}
bool incremental_preparation() {
  using namespace gneiss::engine;
  auto backend = std::make_shared<range_files>();
  mesh_source fixture;
  if (fixture.read("mesh", backend->data->bytes) != gneiss::engine::result::success) {
    return false;
  }
  backend->data->bytes.resize(4096U, std::byte{' '});
  virtual_file_system files;
  if (files.mount("asset://", backend) != gneiss::engine::result::success) {
    return false;
  }
  const std::array requests{
      asset_preparation_source{.uri = "asset://mesh", .type = asset_type::mesh}};
  return run_preparation_case(backend, files, requests, {}) &&
         run_preparation_case(backend, files, requests, {.cancel = true}) &&
         run_preparation_case(backend, files, requests, {.change = true}) &&
         run_preparation_case(backend, files, requests, {.fail = true}) &&
         run_preparation_case(backend, files, requests, {});
}

bool prefetched_preparation(bool deferred_open = false) {
  using namespace gneiss::engine;
  auto backend = std::make_shared<range_files>();
  mesh_source fixture;
  if (fixture.read("mesh", backend->data->bytes) != gneiss::engine::result::success) {
    return false;
  }
  backend->data->deferred = true;
  backend->data->deferred_open = deferred_open;
  virtual_file_system files;
  if (files.mount("asset://", backend) != gneiss::engine::result::success) {
    return false;
  }
  std::vector<asset_preparation_source> requested;
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
    if (preparation.advance(65536U, {}, output, diagnostic, complete) !=
            gneiss::engine::result::success ||
        complete || (deferred_open ? backend->data->live_opens : backend->data->live_reads) != 8U ||
        !output.assets.empty()) {
      return false;
    }
    if (preparation.advance(
            65536U, [] { return true; }, output, diagnostic, complete) !=
            gneiss::engine::result::invalid_state ||
        !complete || backend->data->live_reads != 0U || backend->data->live_opens != 0U) {
      return false;
    }
  }
  // 取消后可用同一后端重试，结果在全部请求解析成功后一次输出。
  backend->data->ready = true;
  asset_preparation preparation(files, requested, {.maximum_assets = 16U, .maximum_bytes = 65536U},
                                {});
  complete = false;
  for (unsigned step = 0U; step < 64U && !complete; ++step) {
    if (preparation.advance(65536U, {}, output, diagnostic, complete) !=
        gneiss::engine::result::success) {
      return false;
    }
  }
  return complete && output.assets.size() == requested.size() && backend->data->live_reads == 0U &&
         backend->data->maximum_live_reads <= 9U && backend->data->live_opens == 0U &&
         (!deferred_open || backend->data->synchronous_opens == 0U);
}
bool deferred_open_failure(bool oversized) {
  using namespace gneiss::engine;
  auto backend = std::make_shared<range_files>();
  backend->data->deferred_open = true;
  backend->data->bytes.resize(128U);
  virtual_file_system files;
  if (files.mount("asset://", backend) != gneiss::engine::result::success) {
    return false;
  }
  const std::array requests{
      asset_preparation_source{.uri = "asset://mesh", .type = asset_type::mesh}};
  asset_preparation preparation(files, requests, {.maximum_assets = 1U, .maximum_bytes = 64U}, {});
  prepared_batch output;
  asset_diagnostic diagnostic;
  bool complete{};
  if (preparation.advance(64U, {}, output, diagnostic, complete) !=
          gneiss::engine::result::success ||
      complete || backend->data->read_bytes != 0U || backend->data->live_opens != 1U) {
    return false;
  }
  backend->data->ready = true;
  backend->data->fail = !oversized;
  const auto expected =
      oversized ? gneiss::engine::result::invalid_argument : gneiss::engine::result::io;
  return preparation.advance(64U, {}, output, diagnostic, complete) == expected && complete &&
         output.assets.empty() && backend->data->live_opens == 0U &&
         backend->data->synchronous_opens == 0U && backend->data->read_bytes == 0U;
}
int verify_incremental_paths() {
  if (!incremental_preparation()) {
    return 12;
  }
  return prefetched_preparation() && prefetched_preparation(true) && deferred_open_failure(false) &&
                 deferred_open_failure(true)
             ? 0
             : 13;
}
} // namespace

int main() try {
  const auto incremental_result = verify_incremental_paths();
  if (incremental_result != 0) {
    return incremental_result;
  }
  using namespace gneiss::engine;
  prepared_batch output;
  asset_diagnostic diagnostic;
  gneiss::engine::result result = gneiss::engine::result::internal;
  // 不创建 Application、缓存或资源服务；后台输出在 VFS 销毁后仍拥有完整数组。
  std::thread worker([&] {
    try {
      gneiss::engine::virtual_file_system files;
      auto source = std::make_shared<mesh_source>();
      if (files.mount("asset://", source) != gneiss::engine::result::success) {
        return;
      }
      const std::array requests{
          asset_preparation_source{.uri = "asset://mesh", .type = asset_type::mesh},
      };
      result = prepare_assets(files, requests, output, diagnostic, {}, 0U);
    } catch (...) {
      result = gneiss::engine::result::out_of_memory;
    }
  });
  worker.join();
  if (result != gneiss::engine::result::success || output.assets.size() != 1U ||
      output.assets[0].mesh.vertices.size() != 3U || output.bytes == 0U ||
      output.assets[0].sources.size() != 1U || !output.assets[0].sources[0].identity.source ||
      output.assets[0].sources[0].identity.uri != "asset://mesh") {
    return 1;
  }

  gneiss::engine::virtual_file_system files;
  auto source = std::make_shared<mesh_source>();
  if (files.mount("asset://", source) != gneiss::engine::result::success) {
    return 2;
  }
  const std::array requests{
      asset_preparation_source{.uri = "asset://mesh", .type = asset_type::mesh},
  };
  if (prepare_assets(
          files, requests, output, diagnostic, [] { return true; }, 0U) !=
          gneiss::engine::result::invalid_state ||
      !output.assets.empty() || output.bytes != 0U) {
    return 3;
  }
  if (prepare_assets(files, requests, output, diagnostic, {}, 0U, 1U, 1U) ==
          gneiss::engine::result::success ||
      !output.assets.empty()) {
    return 4;
  }
  source->change_on_verify = true;
  if (prepare_assets(files, requests, output, diagnostic, {}, 0U) !=
          gneiss::engine::result::success ||
      output.assets.size() != 1U || output.assets[0].mesh.vertices.size() != 3U) {
    return 5;
  }
  const std::array missing{
      asset_preparation_source{.uri = "asset://missing", .type = asset_type::mesh},
  };
  if (prepare_assets(files, missing, output, diagnostic, {}, 0U) !=
          gneiss::engine::result::not_found ||
      !output.assets.empty()) {
    return 6;
  }
  source->change_on_verify = false;
  const std::array material_request{
      asset_preparation_source{.uri = "asset://material", .type = asset_type::material},
  };
  if (prepare_test_materials(files, material_request, output, diagnostic, {}, 1000U, 1U, 1000U) !=
          gneiss::engine::result::success ||
      output.bytes != 1000U || output.assets.size() != 1U) {
    return 8;
  }
  if (prepare_test_materials(files, material_request, output, diagnostic, {}, 0U) !=
          gneiss::engine::result::invalid_argument ||
      !output.assets.empty()) {
    return 9;
  }
  if (prepare_test_materials(files, material_request, output, diagnostic, {}, 1001U, 1U, 1000U) !=
          gneiss::engine::result::out_of_memory ||
      !output.assets.empty()) {
    return 10;
  }
  const std::array linked_request{
      asset_preparation_source{.uri = "asset://linked", .type = asset_type::material},
  };
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  if (prepare_test_materials(files, linked_request, output, diagnostic, {}, maximum, 2U, maximum) !=
          gneiss::engine::result::out_of_memory ||
      !output.assets.empty()) {
    return 11;
  }
  return 0;
} catch (...) {
  return 7;
}
