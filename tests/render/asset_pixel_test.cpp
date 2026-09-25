// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_asset_reload_internal.h"
#include "application/application_scene_load_internal.h"
#include "asset/texture_ktx2.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/application.hpp>
#include <gneiss/scene.h>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("像素验收失败，行=" + std::to_string(where.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-asset-pixel-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    std::ofstream(root / "t.texture.json")
        << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
    std::ofstream(root / "m.material.json")
        << R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://t.texture.json","metallic":0,"roughness":1})";
    std::ofstream(root / "g.mesh.json")
        << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.8,-0.7,0],[0.8,-0.7,0],[0,0.8,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
    std::ofstream(root / "s.scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"camera":{"vertical_field_of_view_radians":1.04719755,"near_plane":0.1,"far_plane":100,"is_primary":true}}},{"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://g.mesh.json","material":"asset://m.material.json"}}}],"prefab_instances":[]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  void color(std::byte red, std::byte blue) {
    std::vector<std::byte> bytes;
    std::string message;
    check(asset_internal::encode_texture_ktx2(
              {.transfer = asset_internal::texture_transfer::srgb,
               .levels = {{.width = 1U,
                           .height = 1U,
                           .pixels = {red, std::byte{20}, blue, std::byte{255}}}}},
              bytes, message) == asset_internal::texture_ktx2_result::success);
    std::ofstream stream(root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
};
void run(tasks::execution_mode mode) {
  fixture files;
  files.color(std::byte{20}, std::byte{240});
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  const auto root = files.root.string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create(desc, app) == result::success);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  gneiss_scene_instance scene{};
  constexpr std::string_view uri = "asset://s.scene.json";
  check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  check(app.run(3U) == result::success);
  const auto capture = [&](unsigned width = 128U) {
    render_internal::frame_image image;
    const auto result = application_internal::capture_frame(app.get(), width, 128U, image);
    if (result != GNEISS_SUCCESS) {
      std::fprintf(stderr, "capture=%d\n", result);
    }
    check(result == GNEISS_SUCCESS && image.pixels.size() == width * 128U * 4U);
    return image;
  };
  const auto center = [](const auto& image) {
    const auto offset = (image.height / 2U * image.width + image.width / 2U) * 4U;
    return std::array{std::to_integer<unsigned>(image.pixels[offset]),
                      std::to_integer<unsigned>(image.pixels[offset + 1U]),
                      std::to_integer<unsigned>(image.pixels[offset + 2U])};
  };
  const auto before = capture();
  const auto blue = center(before);
  std::printf("before=%u,%u,%u\n", blue[0], blue[1], blue[2]);
  check(blue[2] > blue[0] + 30U);
  const auto reload = [&](bool mixed = false) {
    using type = render_internal::render_asset_type;
    const std::vector<render_internal::render_asset_reload> assets =
        mixed
            ? std::vector<render_internal::render_asset_reload>{{"asset://g.mesh.json", type::mesh},
                                                                {"asset://m.material.json",
                                                                 type::material}}
            : std::vector<render_internal::render_asset_reload>{
                  {"asset://t.texture.json", type::texture}};
    std::uint64_t request{};
    check(application_internal::request_render_assets(app.get(), assets, 1U, 1U, request) ==
          GNEISS_SUCCESS);
    asset_internal::texture_load_completion completed;
    bool ready{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(application_internal::poll_textures(app.get(), completed, ready) == GNEISS_SUCCESS);
      std::this_thread::yield();
    }
    return completed.result;
  };
  files.color(std::byte{240}, std::byte{20});
  check(reload() == GNEISS_SUCCESS);
  const auto after = capture();
  const auto red = center(after);
  std::printf("after=%u,%u,%u\n", red[0], red[1], red[2]);
  check(red[0] > red[2] + 30U && before.pixels != after.pixels);
  std::ofstream(files.root / "image.ktx2") << "broken";
  check(reload() != GNEISS_SUCCESS);
  check(capture().pixels == after.pixels);
  const auto resized = capture(192U);
  const auto resized_red = center(resized);
  check(resized_red[0] > resized_red[2] + 30U);
  // 材质引入未显式请求的新依赖，网格同时缩小；一次发布后像素颜色和覆盖范围都必须改变。
  files.color(std::byte{20}, std::byte{240});
  std::ofstream(files.root / "new.texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://new.texture.json","metallic":0,"roughness":1})";
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.3,-0.3,0],[0.3,-0.3,0],[0,0.3,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  check(reload(true) == GNEISS_SUCCESS);
  const auto mixed = capture();
  const auto mixed_color = center(mixed);
  check(mixed_color[2] > mixed_color[0] + 30U);
  const auto coverage = [](const auto& image) {
    std::size_t count{};
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U) {
      if (image.pixels[offset] != image.pixels[0] || image.pixels[offset + 1U] != image.pixels[1] ||
          image.pixels[offset + 2U] != image.pixels[2])
        ++count;
    }
    return count;
  };
  check(coverage(mixed) > 0U && coverage(mixed) < coverage(after) / 2U);
  // 同批有效材质与损坏网格不可部分发布。
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":3,"color":[1,0,0,1],"metallic":0,"roughness":1})";
  std::ofstream(files.root / "g.mesh.json") << "broken";
  check(reload(true) != GNEISS_SUCCESS);
  check(capture().pixels == mixed.pixels);
  check(center(capture(192U))[2] > center(capture(192U))[0] + 30U);
}
void run_scene(tasks::execution_mode mode) {
  using namespace application_internal;
  fixture files;
  files.color(std::byte{20}, std::byte{240});
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  const auto root = files.root.string();
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create(desc, app) == result::success);
  check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss_scene_instance scene{};
  check(gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene) == GNEISS_SUCCESS);
  check(app.run(3U) == result::success);
  const auto capture = [&] {
    render_internal::frame_image image;
    check(capture_frame(app.get(), 128U, 128U, image) == GNEISS_SUCCESS);
    return image.pixels;
  };
  auto active_pixels = capture();
  // 私有 GPU 候选 ready 后旧图像仍完全不变；只允许激活后发布新颜色。
  const auto load = [&](std::uint64_t revision, bool cancel, bool fail) {
    const auto old_scene = scene;
    std::uint64_t request{};
    check(request_scene_load(app.get(), uri, 1U, revision, request) == GNEISS_SUCCESS);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool requested_cancel{};
    scene_load_completion completion;
    while (true) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == tasks::execution_mode::cooperative)
        (void)scheduler.run_ready();
      bool terminal{};
      check(poll_scene_load(app.get(), completion, terminal) == GNEISS_SUCCESS);
      if (terminal) {
        check((fail && completion.progress.phase == scene_load_phase::failed) ||
              (requested_cancel && completion.progress.phase == scene_load_phase::cancelled));
        check(capture() == active_pixels);
        break;
      }
      scene_load_progress progress;
      bool available{};
      check(query_scene_load_progress(app.get(), progress, available) == GNEISS_SUCCESS &&
            available);
      if (progress.phase == scene_load_phase::ready) {
        check(!fail && capture() == active_pixels);
        if (cancel) {
          check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
          requested_cancel = true;
        } else {
          check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
          scene = completion.scene;
          check(poll_scene_load(app.get(), completion, terminal) == GNEISS_SUCCESS);
          check(app.run(3U) == result::success);
          const auto changed = capture();
          check(changed != active_pixels);
          active_pixels = changed;
          std::uint64_t count{};
          check(gneiss_scene_instance_get_node_count(app.get(), old_scene, &count) ==
                GNEISS_ERROR_INVALID_HANDLE);
          break;
        }
      }
      std::this_thread::yield();
    }
    scene_retirement_statistics statistics;
    check(query_scene_retirement(app.get(), statistics) == GNEISS_SUCCESS &&
          statistics.live_resources == 3U && !statistics.pending);
  };
  files.color(std::byte{240}, std::byte{20});
  load(1U, false, false);
  files.color(std::byte{20}, std::byte{240});
  load(2U, true, false);
  std::ofstream(files.root / "image.ktx2") << "broken";
  load(3U, false, true);
  files.color(std::byte{20}, std::byte{240});
  load(4U, false, false);
}
}
int main() try {
  run(gneiss::tasks::execution_mode::thread_pool);
  run(gneiss::tasks::execution_mode::cooperative);
  run_scene(gneiss::tasks::execution_mode::thread_pool);
  run_scene(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
