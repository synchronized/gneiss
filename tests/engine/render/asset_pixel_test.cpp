// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/codec_result.hpp"
#include "../../support/value_result.hpp"
#include "function/asset/detail/asset_construction.hpp"
#include "function/render/detail/frame_packet.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/mesh/mesh_binary.hpp>
#include <gneiss/engine/resource/texture/texture_binary.hpp>
#include <gneiss/engine/resource/texture/texture_ktx2.hpp>
#include <gneiss/engine/resource/texture/texture_view.hpp>
#include <granit/asset_tools/texture_builder.hpp>
#include <granit/renderer/texture_asset.hpp>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("像素验收失败，行=" + std::to_string(where.line()));
  }
}
// 通道对照需要确定已使用真实纹理，而非默认贴图；等待 CPU 请求后用完成的回读跨过上传提交。
void prepare_texture_capture(runtime::application& app) {
  auto request = app.assets().load<texture>("asset://t.texture.json");
  check(bool(request));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (request->status() == error::not_ready) {
    check(app.run(1).ok());
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  check(bool(request->take()));
  gneiss::render::frame_image submitted;
  check(app.capture_frame(128U, 128U, submitted).ok());
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-asset-pixel-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  explicit fixture(bool pbr = false) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "t.texture.json")
        << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
    std::ofstream(root / "m.material.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"color":[1,1,1,1],"metallic":0,"roughness":1},"texture_uris":["asset://t.texture.json","","","",""]})";
    if (pbr) {
      std::ofstream(root / "m.material.json")
          << R"({"format":"gneiss.material","version":6,"parameters":{"color":[0,0,0,1],"metallic":0,"roughness":1,"emissive":[1,1,1]},"texture_uris":["","","","","asset://t.texture.json"]})";
    }
    std::ofstream(root / "g.mesh.json")
        << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.8,-0.7,0],[0.8,-0.7,0],[0,0.8,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
    std::ofstream(root / "s.scene.json")
        << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0101","$content":{"settings":{"fov":1.04719755,"near":0.1,"far":100},"is_primary":true,"uuid":"06124299-d9b6-484c-b365-af2669a4214d"}}]},{"uuid":"00000000-0000-4000-8000-000000000003","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://g.mesh.json","material":"asset://m.material.json","uuid":"c2629bbb-e785-4d94-9414-518ad9496590"}}]}]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  void color(std::byte red, std::byte blue) {
    std::vector<std::byte> bytes;
    std::string message;
    check(gneiss::test_support::take_codec(
              gneiss::encode_texture_ktx2(
                  {.transfer = gneiss::texture_transfer::srgb,
                   .levels = {{.width = 1U,
                               .height = 1U,
                               .pixels = {red, std::byte{20}, blue, std::byte{255}}}}}),
              bytes, message) == std::nullopt);
    std::ofstream stream(root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
};
void run_asset_reload() {
  fixture files(true);
  files.color(std::byte{255}, std::byte{0});
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  check(app->attach_task_executor(scheduler).ok());
  const auto reload = [&](std::span<const gneiss::render_resource_source> requests) {
    gneiss::asset_batch_request assets_request;
    auto status =
        gneiss::test_support::take_value(app->load_assets(requests, true), assets_request);
    if (status.failed())
      return status;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      (void)scheduler.run_ready();
      gneiss::asset_load_completion completion;
      bool ready{};
      status = app->run(1);
      if (status.failed())
        return status;
      status = gneiss::test_support::receive_request(assets_request, completion, ready);
      if (status.failed() || ready)
        return status.failed() ? status : completion.result;
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance scene{};
  check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
        gneiss::error::success);
  // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
  bool selected_camera{};
  for (auto object : scene.objects()) {
    if (object.try_component<camera_component>()) {
      check(set_active_camera(*app->native_world(), object.entity()).ok());
      selected_camera = true;
    }
  }
  check(selected_camera);
  const auto pixel = [&] {
    gneiss::render::frame_image image;
    check(app->capture_frame(128U, 128U, image) == gneiss::error::success);
    constexpr auto offset = ((64U * 128U) + 64U) * 4U;
    return std::array{
        std::to_integer<unsigned>(image.pixels[offset]),
        std::to_integer<unsigned>(image.pixels[offset + 2U]),
    };
  };
  const auto await_color = [&](bool blue) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto value = pixel();
    while (value[blue ? 1U : 0U] <= value[blue ? 0U : 1U] + 30U) {
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error("等待纹理重载颜色超时：期望=" + std::string(blue ? "蓝" : "红") +
                                 "，实际 R=" + std::to_string(value[0]) +
                                 " B=" + std::to_string(value[1]));
      }
      // 材质完成不代表纹理依赖已完成；协作式执行器也必须继续驱动。
      (void)scheduler.run_ready();
      check(app->run(1).ok());
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      value = pixel();
    }
    return value;
  };
  const auto initial = await_color(false);
  check(initial[0] > initial[1] + 30U);
  files.color(std::byte{0}, std::byte{255});
  const std::array requests{gneiss::render_resource_source{
      .uri = "asset://t.texture.json", .type = gneiss::render_resource_kind::texture}};
  check(reload(requests) == gneiss::error::success);
  // 不重载场景、不重建材质：已有引用必须看到同一纹理 RID 的新内容。
  const auto updated = await_color(true);
  check(updated[1] > updated[0] + 30U);
  std::ofstream(files.root / "t.texture.json") << "{";
  check(reload(requests) == gneiss::error::invalid_argument);
  check(pixel() == updated);
}

void run_pending_resources() {
  fixture files(true);
  files.color(std::byte{255}, std::byte{0});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance scene{};
  check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene)} ==
        gneiss::error::success);
  // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
  bool selected_camera{};
  for (auto object : scene.objects()) {
    if (object.try_component<camera_component>()) {
      check(set_active_camera(*app->native_world(), object.entity()).ok());
      selected_camera = true;
    }
  }
  check(selected_camera);
  auto native = app;
  check(native != nullptr);
  auto& resources = native->render();
  // 此用例直接验证后端未就绪身份与后续内容替换，使用内部构建器保留预留阶段。
  gneiss::detail::asset_construction resources_factory{resources};
  gneiss::ref<const gneiss::mesh> mesh_ref;
  gneiss::ref<const gneiss::texture> texture_ref;
  check(gneiss::test_support::take_value(resources_factory.reserve_mesh(), mesh_ref) ==
        gneiss::error::success);
  check(gneiss::test_support::take_value(resources_factory.reserve_texture(), texture_ref) ==
        gneiss::error::success);
  const auto mesh = resource_handle(mesh_ref);
  const auto texture = resource_handle(texture_ref);
  gneiss::material_data material_desc;
  material_desc.parameters.color[0] = material_desc.parameters.color[1] =
      material_desc.parameters.color[2] = 0.0F;
  material_desc.parameters.emissive[0] = material_desc.parameters.emissive[1] =
      material_desc.parameters.emissive[2] = 1.0F;
  material_desc.emissive_texture = texture;
  gneiss::ref<const gneiss::material> material_ref;
  check(gneiss::error{gneiss::test_support::take_value(
            resources_factory.create_material(
                material_desc.parameters,
                {gneiss::ref<const gneiss::texture>{}, {}, {}, {}, texture_ref}),
            material_ref)} == gneiss::error::success);
  gneiss::game_object mesh_object;
  gneiss::mesh_component* target{};
  for (auto object : scene.objects()) {
    if (auto* component = object.try_component<gneiss::mesh_component>()) {
      check(target == nullptr);
      target = component;
      mesh_object = object;
    }
  }
  const gneiss::mesh_component binding{.mesh = mesh_ref, .material = material_ref};
  check(target != nullptr);
  *target = binding;
  const auto pixel = [&] {
    gneiss::render::frame_image image;
    check(app->capture_frame(128U, 128U, image) == gneiss::error::success);
    constexpr auto offset = ((64U * 128U) + 64U) * 4U;
    return std::array{
        std::to_integer<unsigned>(image.pixels[offset]),
        std::to_integer<unsigned>(image.pixels[offset + 1U]),
        std::to_integer<unsigned>(image.pixels[offset + 2U]),
    };
  };
  const auto hidden = pixel();
  auto geometry = std::make_shared<gneiss::mesh_data>();
  geometry->vertices = {
      {.position = {-0.8F, -0.7F, 0.0F}, .uv = {0.0F, 0.0F}},
      {.position = {0.8F, -0.7F, 0.0F}, .uv = {1.0F, 0.0F}},
      {.position = {0.0F, 0.8F, 0.0F}, .uv = {0.5F, 1.0F}},
  };
  geometry->normals.assign(3U, {0.0F, 0.0F, 1.0F});
  check(resources.replace_mesh(mesh, geometry).ok());
  const auto placeholder = pixel();
  check(placeholder[0] > hidden[0] + 30U && placeholder[1] > hidden[1] + 30U &&
        placeholder[2] > hidden[2] + 30U);
  auto red = std::make_shared<gneiss::texture_data>();
  red->width = red->height = 1U;
  red->format = gneiss::texture_format::rgba8_unorm;
  red->color_space = gneiss::texture_color_space::srgb;
  red->levels = {
      {
          .width = 1U,
          .height = 1U,
          .pixels = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}},
      },
  };
  check(resources.replace_texture(texture, red).ok());
  const auto await_pixel = [&](bool is_blue) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto color = pixel();
    const auto expected = is_blue ? 2U : 0U;
    const auto other = is_blue ? 0U : 2U;
    while (!(color[expected] > color[1] + 30U && color[expected] > color[other] + 30U) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      color = pixel();
    }
    return color;
  };
  const auto ready = await_pixel(false);
  check(ready[0] > ready[1] + 30U && ready[0] > ready[2] + 30U);
  auto blue = std::make_shared<gneiss::texture_data>(*red);
  blue->levels.front().pixels = {std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
  check(resources.replace_texture(texture, blue).ok());
  const auto updated = await_pixel(true);
  check(updated[2] > updated[0] + 30U && updated[2] > updated[1] + 30U);
  // 无效完成不能覆盖旧内容；交还 RID 后已有材质依赖仍保留内容。
  check(!resources.replace_texture(texture, {}));
  check(!resources.replace_mesh(mesh, {}));
  check(pixel() == updated);
  check(gneiss::error{resources.destroy_texture(texture)} == gneiss::error::success);
  check(resources.texture_status(texture) == gneiss::error::invalid_handle);
  check(pixel() == updated);
  check(!resources.replace_texture(texture, red));
  check(gneiss::error{mesh_object.remove_component<gneiss::mesh_component>()} ==
        gneiss::error::success);
  check(pixel() == hidden);
  check(gneiss::error{resources.destroy_material(resource_handle(material_ref))} ==
        gneiss::error::success);
  check(gneiss::error{resources.destroy_mesh(mesh)} == gneiss::error::success);
}

void run_mip_sampling(bool packaged = false, bool async = false) {
  fixture files(true);
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.8,-0.7,0],[0.8,-0.7,0],[0,0.8,0]],"uvs":[[0,0],[1024,0],[512,1024]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  gneiss::texture_ktx2 texture;
  texture.transfer = gneiss::texture_transfer::srgb;
  for (std::uint32_t width = 8U; width > 0U; width /= 2U) {
    gneiss::texture_mip mip{.width = width, .height = width, .pixels = {}};
    for (std::uint32_t pixel = 0; pixel < width * width; ++pixel) {
      mip.pixels.insert(mip.pixels.end(),
                        {width == 8U ? std::byte{0} : std::byte{255}, std::byte{0},
                         width == 8U ? std::byte{255} : std::byte{0}, std::byte{255}});
    }
    texture.levels.push_back(std::move(mip));
  }
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(gneiss::test_support::take_codec(gneiss::encode_texture_ktx2(texture), bytes, diagnostic) ==
        std::nullopt);
  if (packaged) {
    std::vector<std::byte> payload;
    std::vector<granit::asset_tools::texture::subresource_info> subresources;
    for (std::uint32_t mip = 0; mip < texture.levels.size(); ++mip) {
      const auto& level = texture.levels[mip];
      subresources.push_back({.mip_level = mip,
                              .array_layer = 0U,
                              .data_offset = payload.size(),
                              .data_size = level.pixels.size(),
                              .bytes_per_row = level.width * 4U,
                              .rows_per_image = level.height});
      payload.insert(payload.end(), level.pixels.begin(), level.pixels.end());
    }
    auto unused = payload;
    for (auto& value : unused) {
      value = std::byte{};
    }
    // 第一个变体不允许采样，必须选择非零偏移的第二个 RGBA8 变体。
    const std::array variants{
        granit::asset_tools::texture::variant_desc{.format = granit::texture_format::rgba8_srgb,
                                                   .usage =
                                                       granit::texture_usage::transfer_destination,
                                                   .payload = unused,
                                                   .subresources = subresources},
        granit::asset_tools::texture::variant_desc{
            .format = granit::texture_format::rgba8_srgb,
            .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
            .payload = payload,
            .subresources = subresources},
    };
    const auto [status, built] = granit::asset_tools::texture::build(
        {.dimension = granit::texture_dimension::two_dimensional,
         .width = 8U,
         .height = 8U,
         .depth = 1U,
         .array_layers = 1U,
         .mip_levels = static_cast<std::uint32_t>(texture.levels.size()),
         .variants = variants});
    check(status == granit::result::success);
    granit::texture_asset_info info;
    check(granit::inspect_texture_asset(built.manifest(), info) == granit::result::success);
    check(info.variants.size() == 2U && info.variants[1].payload_offset > 0U);
    auto encoded = gneiss::encode_texture_binary(built.manifest(), built.payload());
    check(encoded.ok());
    bytes = std::move(*encoded);
    std::ofstream(files.root / "t.texture.json")
        << R"({"format":"gneiss.texture","version":2,"source":"asset://image.gneiss-texture","color_space":"srgb","usage":"default"})";
  }
  {
    std::ofstream stream(files.root / (packaged ? "image.gneiss-texture" : "image.ktx2"),
                         std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  gneiss::tasks::task_scheduler scheduler({.workers = 1U});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance scene{};
  if (async) {
    check(app->attach_task_executor(scheduler).ok());
    auto request = app->assets().load<gneiss::scene>(uri);
    check(bool(request));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (request->status() == gneiss::error::not_ready) {
      check(std::chrono::steady_clock::now() < deadline);
      check(app->run(1).ok());
      std::this_thread::yield();
    }
    auto content = request->snapshot();
    check(bool(request->take()) && bool(content));
    auto activated = test_support::activate_scene(*app, *content);
    check(bool(activated));
    scene = activated->instance.instance;
    // CPU 场景先实例化，随后分发视觉加载；两帧图像必须体现资源就绪后的变化。
    gneiss::render::frame_image initial;
    check(app->capture_frame(128U, 128U, initial).ok());
    while (activated->visuals && !activated->visuals.ready()) {
      check(std::chrono::steady_clock::now() < deadline);
      check(app->run(1).ok());
      std::this_thread::yield();
    }
    check(bool(activated->visuals));
    auto completed = activated->visuals.take();
    check(bool(completed) && completed->result.ok());
    gneiss::render::frame_image loaded;
    check(app->capture_frame(128U, 128U, loaded).ok());
    check(initial.pixels != loaded.pixels);
  } else {
    check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
          gneiss::error::success);
    // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
    bool selected_camera{};
    for (auto object : scene.objects()) {
      if (object.try_component<camera_component>()) {
        check(set_active_camera(*app->native_world(), object.entity()).ok());
        selected_camera = true;
      }
    }
    check(selected_camera);
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)scheduler.run_ready();
    check(app->run(1).ok());
    gneiss::render::frame_image image;
    check(app->capture_frame(128U, 128U, image).ok());
    const auto offset = (64U * 128U + 64U) * 4U;
    // LOD0 全蓝，缩小时应采样全红的较低分辨率层；等待真实采样而非固定帧数。
    if (std::to_integer<unsigned>(image.pixels[offset]) >
        std::to_integer<unsigned>(image.pixels[offset + 2U]) + 30U)
      break;
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}

std::array<unsigned, 3> material_pixel(std::string_view slot, std::array<std::byte, 4> pixel,
                                       bool reflected = false) {
  fixture files;
  std::ofstream(files.root / "t.texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"linear","usage":"default"})";
  std::ofstream material(files.root / "m.material.json");
  material
      << R"({"format":"gneiss.material","version":6,"parameters":{"color":[0.5,0.5,0.5,1],"metallic":1,"roughness":1,"emissive":[)"
      << (slot == "emissive_texture" ? "1,1,1" : "0,0,0") << R"(]},"texture_uris":[)";
  constexpr std::array slots{"base_color_texture", "metallic_roughness_texture", "normal_texture",
                             "occlusion_texture", "emissive_texture"};
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (i)
      material << ',';
    material << (slot == slots[i] ? R"("asset://t.texture.json")" : R"("")");
  }
  material << "]}";
  material.close();
  gneiss::mesh_binary_data mesh{.vertices = {{{-0.8F, -0.7F, 0.0F}, {0, 0}, {0, 0, 1}},
                                             {{0.8F, -0.7F, 0.0F}, {1, 0}, {0, 0, 1}},
                                             {{0.0F, 0.8F, 0.0F}, {0.5F, 1}, {0, 0, 1}}},
                                .indices = {0U, 1U, 2U},
                                .tangents = {{1, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}}};
  std::vector<std::byte> bytes;
  gneiss::mesh_binary_diagnostic mesh_diagnostic;
  check(gneiss::test_support::take_codec(gneiss::encode_mesh_binary(mesh), bytes,
                                         mesh_diagnostic) == std::nullopt);
  {
    std::ofstream stream(files.root / "g.gneiss-mesh", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  {
    std::ifstream stream(files.root / "s.scene.json");
    std::string scene{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    stream.close();
    scene.replace(scene.find("g.mesh.json"), 11U, "g.gneiss-mesh");
    if (reflected) {
      constexpr std::string_view scale = "\"scale\":[1,1,1]";
      scene.replace(scene.rfind(scale), scale.size(), "\"scale\":[-1,1,1]");
    }
    std::ofstream(files.root / "s.scene.json") << scene;
  }
  std::string diagnostic;
  check(gneiss::test_support::take_codec(
            gneiss::encode_texture_ktx2(
                {.transfer = gneiss::texture_transfer::linear,
                 .levels = {{.width = 1U, .height = 1U, .pixels = {pixel.begin(), pixel.end()}}}}),
            bytes, diagnostic) == std::nullopt);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance scene{};
  check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene)} ==
        gneiss::error::success);
  // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
  bool selected_camera{};
  for (auto object : scene.objects()) {
    if (object.try_component<camera_component>()) {
      check(set_active_camera(*app->native_world(), object.entity()).ok());
      selected_camera = true;
    }
  }
  check(selected_camera);
  prepare_texture_capture(*app);
  gneiss::render::frame_image image;
  check(app->capture_frame(128U, 128U, image) == gneiss::error::success);
  const auto offset = (64U * 128U + 64U) * 4U;
  return {std::to_integer<unsigned>(image.pixels[offset]),
          std::to_integer<unsigned>(image.pixels[offset + 1U]),
          std::to_integer<unsigned>(image.pixels[offset + 2U])};
}

struct state_case {
  std::string_view mode{"OPAQUE"};
  float alpha{1};
  float cutoff{0.5F};
  bool back{};
  bool double_sided{};
  std::array<float, 4> color{1, 1, 1, 1};
  unsigned uv_set{};
  unsigned wrap{};
  bool layers{};
  bool reverse_submission{};
  bool foreground{};
  bool receiver{};
};
std::array<unsigned, 3> state_pixel(const state_case& test,
                                    std::vector<std::byte>* frame = nullptr) {
  std::fprintf(
      stderr,
      "material-state mode=%.*s alpha=%g back=%d double=%d uv=%u wrap=%u layers=%d receiver=%d\n",
      static_cast<int>(test.mode.size()), test.mode.data(), static_cast<double>(test.alpha),
      test.back, test.double_sided, test.uv_set, test.wrap, test.layers, test.receiver);
  fixture files;
  std::ofstream material(files.root / "m.material.json");
  material << R"({"format":"gneiss.material","version":6,"parameters":{"color":[1,1,1,)"
           << test.alpha << R"(],"metallic":0,"roughness":1,"alpha_mode":)"
           << (test.mode == "MASK"    ? 1
               : test.mode == "BLEND" ? 2
                                      : 0)
           << R"(,"alpha_cutoff":)" << test.cutoff << R"(,"double_sided":)"
           << (test.double_sided ? "true" : "false") << R"(,"sampling":[)";
  for (unsigned i = 0; i < 5; ++i) {
    material << (i == 0 ? "" : ",") << R"({"uv_set":)" << (i == 0 ? test.uv_set : 0)
             << R"(,"mag_filter":0,"min_filter":0,"mip_filter":0,"address_u":)" << test.wrap
             << R"(,"address_v":0})";
  }
  material << R"(]},"texture_uris":["asset://t.texture.json","","","",""]})";
  material.close();
  gneiss::mesh_binary_data mesh;
  mesh.vertices = {{{-0.8F, -0.7F, 0}, {1.25F, 0.5F}, {0, 0, 1}},
                   {{0.8F, -0.7F, 0}, {1.25F, 0.5F}, {0, 0, 1}},
                   {{0, 0.8F, 0}, {1.25F, 0.5F}, {0, 0, 1}}};
  mesh.indices =
      test.back ? std::vector<std::uint32_t>{2, 1, 0} : std::vector<std::uint32_t>{0, 1, 2};
  mesh.tangents.assign(3, {1, 0, 0, 1});
  mesh.uv1.assign(3, {0.75F, 0.5F});
  mesh.colors.assign(3, {test.color[0], test.color[1], test.color[2], test.color[3]});
  std::vector<std::byte> bytes;
  gneiss::mesh_binary_diagnostic diagnostic;
  check(gneiss::test_support::take_codec(gneiss::encode_mesh_binary(mesh), bytes, diagnostic) ==
        std::nullopt);
  {
    std::ofstream output(files.root / "g.mesh.json", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::string message;
  check(
      gneiss::test_support::take_codec(
          gneiss::encode_texture_ktx2(
              {.transfer = gneiss::texture_transfer::srgb,
               .levels = {{.width = 2,
                           .height = 1,
                           .pixels = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                                      std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}}},
                          {.width = 1,
                           .height = 1,
                           .pixels = {std::byte{188}, std::byte{0}, std::byte{188},
                                      std::byte{255}}}}}),
          bytes, message) == std::nullopt);
  {
    std::ofstream output(files.root / "image.ktx2", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  if (test.layers || test.receiver) {
    std::ofstream(files.root / "blue.material.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"color":[0,0,1,)"
        << (test.foreground || test.receiver ? "1" : "0.5")
        << R"(],"metallic":0,"roughness":1,"alpha_mode":)"
        << (test.foreground || test.receiver ? 0 : 2) << R"(},"texture_uris":["","","","",""]})";
    std::ifstream input(files.root / "s.scene.json");
    std::string scene_text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    input.close();
    std::string object =
        R"({"uuid":"00000000-0000-4000-8000-000000000004","parent":{"object":null,"source":null},"transform":{"translation":[0,0,)";
    object += test.foreground ? "0.2" : "-0.2";
    object +=
        R"(],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://g.mesh.json","material":"asset://blue.material.json","uuid":"9d570935-e6d2-42e7-8132-e19bafd22f70"}}]})";
    if (test.receiver) {
      constexpr std::string_view scale = "\"scale\":[1,1,1]";
      object.replace(object.find(scale), scale.size(), "\"scale\":[4,4,1]");
    }
    if (test.reverse_submission)
      scene_text.insert(scene_text.find("\"objects\":[") + 11U, object + ',');
    else {
      const auto objects_end = scene_text.rfind("]}");
      check(objects_end != std::string::npos);
      scene_text.insert(objects_end, ',' + object);
    }
    std::ofstream(files.root / "s.scene.json") << scene_text;
  }
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance scene{};
  check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene)} ==
        gneiss::error::success);
  // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
  bool selected_camera{};
  for (auto object : scene.objects()) {
    if (object.try_component<camera_component>()) {
      check(set_active_camera(*app->native_world(), object.entity()).ok());
      selected_camera = true;
    }
  }
  check(selected_camera);
  prepare_texture_capture(*app);
  gneiss::render::frame_image image;
  check(app->capture_frame(128U, 128U, image) == gneiss::error::success);
  if (frame != nullptr)
    *frame = image.pixels;
  constexpr auto offset = (64U * 128U + 64U) * 4U;
  return {std::to_integer<unsigned>(image.pixels[offset]),
          std::to_integer<unsigned>(image.pixels[offset + 1]),
          std::to_integer<unsigned>(image.pixels[offset + 2])};
}
void run_material_states() {
  const auto opaque = state_pixel({});
  const auto background = state_pixel({.mode = "MASK", .alpha = 0.25F});
  check(opaque != background);
  check(state_pixel({.mode = "MASK", .alpha = 0.75F}) == opaque);
  check(state_pixel({.mode = "MASK", .alpha = 0.75F, .cutoff = 0.9F}) == background);
  check(state_pixel({.back = true}) == background);
  check(state_pixel({.back = true, .double_sided = true}) != background);
  check(state_pixel({.color = {0, 0, 0, 1}})[0] < opaque[0]);
  check(state_pixel({.mode = "MASK", .color = {1, 1, 1, 0.25F}}) == background);
  const auto uv1 = state_pixel({.uv_set = 1});
  check(opaque[0] > opaque[2] + 30U && uv1[2] > uv1[0] + 30U);
  check(state_pixel({.wrap = 1}) == uv1);
  check(state_pixel({.wrap = 2}) == uv1);
  check(state_pixel({.mode = "BLEND", .alpha = 0}) == background);
  const auto blend = state_pixel({.mode = "BLEND", .alpha = 0.5F});
  check(blend != opaque && blend != background);
  const auto layers = state_pixel({.mode = "BLEND", .alpha = 0.5F, .layers = true});
  check(layers ==
        state_pixel({.mode = "BLEND", .alpha = 0.5F, .layers = true, .reverse_submission = true}));
  check(layers[0] > layers[2]);
  const auto foreground =
      state_pixel({.mode = "BLEND", .alpha = 0.5F, .layers = true, .foreground = true});
  check(foreground[2] > foreground[0] + 30U);
  // 透明 Alpha 0 不投影；MASK 裁掉的整个物体也不能留下实心阴影。
  std::vector<std::byte> invisible_blend, invisible_mask, opaque_shadow, mask_shadow;
  (void)state_pixel({.mode = "BLEND", .alpha = 0, .receiver = true}, &invisible_blend);
  (void)state_pixel({.mode = "MASK", .alpha = 0, .receiver = true}, &invisible_mask);
  (void)state_pixel({.receiver = true}, &opaque_shadow);
  (void)state_pixel({.mode = "MASK", .receiver = true}, &mask_shadow);
  check(invisible_blend == invisible_mask);
  check(opaque_shadow == mask_shadow && opaque_shadow != invisible_mask);
}

void run_material_channels() {
  constexpr auto zero = std::byte{0};
  constexpr auto half = std::byte{128};
  constexpr auto full = std::byte{255};
  // 通道契约：MR 的 R 无效、G/B 有效；AO 只读取 R。
  const auto dielectric = material_pixel("metallic_roughness_texture", {zero, full, zero, full});
  check(dielectric == material_pixel("metallic_roughness_texture", {full, full, zero, full}));
  check(dielectric != material_pixel("metallic_roughness_texture", {zero, half, zero, full}));
  check(dielectric != material_pixel("metallic_roughness_texture", {zero, full, full, full}));
  const auto occluded = material_pixel("occlusion_texture", {zero, full, full, full});
  check(occluded == material_pixel("occlusion_texture", {zero, zero, zero, full}));
  check(occluded[0] < material_pixel("occlusion_texture", {full, full, full, full})[0]);
  // 明确的 +Y/-Y 法线在固定非对称灯光下不能产生同一结果。
  check(material_pixel("normal_texture", {half, full, half, full}) !=
        material_pixel("normal_texture", {half, zero, half, full}));
}

void run(gneiss::tasks::execution_mode mode, bool pbr = false) {
  fixture files(pbr);
  files.color(std::byte{20}, std::byte{240});
  gneiss::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::error::success);
  gneiss::scene_instance scene{};
  constexpr std::string_view uri = "asset://s.scene.json";
  check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
        gneiss::error::success);
  // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
  bool selected_camera{};
  for (auto object : scene.objects()) {
    if (object.try_component<camera_component>()) {
      check(set_active_camera(*app->native_world(), object.entity()).ok());
      selected_camera = true;
    }
  }
  check(selected_camera);
  check(gneiss::error{app->run(3U)} == gneiss::error::success);
  const auto capture = [&](unsigned width = 128U) {
    gneiss::render::frame_image image;
    const auto result = app->capture_frame(width, 128U, image);
    if (result != gneiss::error::success) {
      std::fprintf(stderr, "capture=%d\n", result.code());
    }
    check(result == gneiss::error::success && image.pixels.size() == width * 128U * 4U);
    return image;
  };
  const auto center = [](const auto& image) {
    const auto offset = (image.height / 2U * image.width + image.width / 2U) * 4U;
    return std::array{std::to_integer<unsigned>(image.pixels[offset]),
                      std::to_integer<unsigned>(image.pixels[offset + 1U]),
                      std::to_integer<unsigned>(image.pixels[offset + 2U])};
  };
  const auto await_capture = [&](bool is_blue) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto image = capture();
    auto color = center(image);
    while (!(is_blue ? color[2] > color[0] + 30U : color[0] > color[2] + 30U) &&
           std::chrono::steady_clock::now() < deadline) {
      (void)scheduler.run_ready();
      check(app->run(1).ok());
      std::this_thread::yield();
      image = capture();
      color = center(image);
    }
    return image;
  };
  const auto before = await_capture(true);
  const auto blue = center(before);
  std::printf("before=%u,%u,%u\n", blue[0], blue[1], blue[2]);
  check(blue[2] > blue[0] + 30U);
  const auto reload = [&](bool mixed = false) {
    using type = gneiss::render_resource_kind;
    const std::vector<gneiss::render_resource_source> assets =
        mixed ? std::vector<gneiss::render_resource_source>{{"asset://g.mesh.json", type::mesh},
                                                            {"asset://m.material.json",
                                                             type::material}}
              : std::vector<gneiss::render_resource_source>{
                    {"asset://t.texture.json", type::texture}};
    gneiss::asset_batch_request assets_request;
    check(gneiss::test_support::take_value(app->load_assets(assets, true), assets_request) ==
          gneiss::error::success);
    gneiss::asset_load_completion completed;
    bool ready{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      check(app->run(1).ok());
      check(gneiss::test_support::receive_request(assets_request, completed, ready) ==
            gneiss::error::success);
      std::this_thread::yield();
    }
    return completed.result;
  };
  files.color(std::byte{240}, std::byte{20});
  check(reload() == gneiss::error::success);
  const auto after = await_capture(false);
  const auto red = center(after);
  std::printf("after=%u,%u,%u\n", red[0], red[1], red[2]);
  check(red[0] > red[2] + 30U && before.pixels != after.pixels);
  std::ofstream(files.root / "image.ktx2") << "broken";
  check(reload() != gneiss::error::success);
  check(capture().pixels == after.pixels);
  const auto resized = capture(192U);
  const auto resized_red = center(resized);
  check(resized_red[0] > resized_red[2] + 30U);
  // 材质引入新依赖，网格同时缩小；各资源完成后像素颜色和覆盖范围都必须改变。
  files.color(std::byte{20}, std::byte{240});
  std::ofstream(files.root / "new.texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})";
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"color":[1,1,1,1],"metallic":0,"roughness":1},"texture_uris":["asset://new.texture.json","","","",""]})";
  std::ofstream(files.root / "g.mesh.json")
      << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[-0.3,-0.3,0],[0.3,-0.3,0],[0,0.3,0]],"uvs":[[0,0],[1,0],[0.5,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  check(reload(true) == gneiss::error::success);
  const auto mixed = await_capture(true);
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
  std::ifstream mesh_input(files.root / "g.mesh.json");
  const std::string valid_mesh{std::istreambuf_iterator<char>{mesh_input},
                               std::istreambuf_iterator<char>{}};
  mesh_input.close();
  // 批次逐项采用：网格失败保留旧几何，成功材质仍更新，不提供跨资源回滚。
  std::ofstream(files.root / "m.material.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"color":[1,0,0,1],"metallic":0,"roughness":1},"texture_uris":["","","","",""]})";
  std::ofstream(files.root / "g.mesh.json") << "broken";
  check(reload(true) != gneiss::error::success);
  const auto partial = await_capture(false);
  check(center(partial)[0] > center(partial)[2] + 30U);
  check(coverage(partial) > 0U && coverage(partial) < coverage(after) / 2U);
  // 恢复相同网格后再次成功重载：同材质的完整图像必须与失败时保留的几何一致。
  std::ofstream(files.root / "g.mesh.json") << valid_mesh;
  check(reload(true) == gneiss::error::success);
  check(capture().pixels == partial.pixels);
  const auto partial_resized = center(capture(192U));
  check(partial_resized[0] > partial_resized[2] + 30U);
}
void run_scene(gneiss::tasks::execution_mode mode, bool pbr = false) {
  using namespace runtime;
  fixture files(pbr);
  files.color(std::byte{20}, std::byte{240});
  gneiss::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  const auto root = files.root.string();
  runtime::application_configuration desc;
  desc.enable_window = true;
  desc.window.visible = false;
  desc.asset_root = root;
  check(gneiss::error{gneiss::test_support::assign_value(runtime::application::create(desc),
                                                         app)} == gneiss::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::error::success);
  constexpr std::string_view uri = "asset://s.scene.json";
  gneiss::scene_instance scene{};
  check(gneiss::error{gneiss::test_support::load_scene(*app, uri, &scene, &scheduler)} ==
        gneiss::error::success);
  // 此夹具只有一台相机；实例化不负责选择宿主的默认渲染相机。
  bool selected_camera{};
  for (auto object : scene.objects()) {
    if (object.try_component<camera_component>()) {
      check(set_active_camera(*app->native_world(), object.entity()).ok());
      selected_camera = true;
    }
  }
  check(selected_camera);
  check(gneiss::error{app->run(3U)} == gneiss::error::success);
  const auto capture = [&] {
    gneiss::render::frame_image image;
    check(app->capture_frame(128U, 128U, image) == gneiss::error::success);
    return image.pixels;
  };
  auto active_pixels = capture();
  const auto initial_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  constexpr auto center = (64U * 128U + 64U) * 4U;
  while (std::to_integer<unsigned>(active_pixels[center + 2U]) <=
         std::to_integer<unsigned>(active_pixels[center]) + 30U) {
    check(std::chrono::steady_clock::now() < initial_deadline);
    (void)scheduler.run_ready();
    check(app->run(1).ok());
    active_pixels = capture();
  }
  // 打开场景只复用已有成功内容；外部文件更新由显式重载处理。
  const auto load = [&](std::uint64_t, bool cancel) {
    const auto old_scene = scene;
    auto request = app->assets().load<gneiss::scene>(uri);
    check(bool(request));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto step = [&] {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::tasks::execution_mode::cooperative)
        (void)scheduler.run_ready();
      check(app->run(1).ok());
      std::this_thread::yield();
    };
    while (request->status() == gneiss::error::not_ready)
      step();
    check(request->status().ok());
    check(capture() == active_pixels);
    if (cancel) {
      request->cancel();
      check(!bool(*request));
      check(capture() == active_pixels);
      check(old_scene.is_valid());
    } else {
      auto content = request->snapshot();
      check(bool(request->take()) && bool(content));
      auto activated = test_support::activate_scene(*app, *content);
      check(bool(activated));
      old_scene.unload();
      scene = activated->instance.instance;
      while (activated->visuals && !activated->visuals.ready())
        step();
      if (activated->visuals) {
        auto completed = activated->visuals.take();
        check(bool(completed) && completed->result.ok());
      }
      check(app->run(3U).ok());
      const auto changed = capture();
      check(changed == active_pixels);
      active_pixels = changed;
      check(!old_scene.is_valid());
    }
    check(app->render().live_resource_count() == 3U);
  };
  files.color(std::byte{240}, std::byte{20});
  load(1U, false);
  files.color(std::byte{20}, std::byte{240});
  load(2U, true);
  std::ofstream(files.root / "image.ktx2") << "broken";
  load(3U, false);
  files.color(std::byte{20}, std::byte{240});
  load(4U, false);
}
}
int main(int argc, char* argv[]) try {
  if (argc == 2 && std::string_view{argv[1]} == "--probe-mask") {
    const auto pixel = state_pixel({.mode = "MASK", .alpha = 0.25F});
    std::printf("mask probe: %u,%u,%u\n", pixel[0], pixel[1], pixel[2]);
    return 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--probe-negative-scale") {
    const auto white = std::array{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    const auto positive_visible = material_pixel("emissive_texture", white);
    const auto negative_visible = material_pixel("emissive_texture", white, true);
    std::printf("visibility control: %u,%u,%u / %u,%u,%u\n", positive_visible[0],
                positive_visible[1], positive_visible[2], negative_visible[0], negative_visible[1],
                negative_visible[2]);
    check(positive_visible[0] > 100U && negative_visible[0] > 100U);
    const auto normal = std::array{std::byte{128}, std::byte{255}, std::byte{128}, std::byte{255}};
    const auto positive = material_pixel("normal_texture", normal);
    const auto negative = material_pixel("normal_texture", normal, true);
    std::printf(
        "negative-scale probe: visible=%u/%u normal-positive=%u,%u,%u normal-negative=%u,%u,%u\n",
        positive_visible[0], negative_visible[0], positive[0], positive[1], positive[2],
        negative[0], negative[1], negative[2]);
    // 保留相同网格绕序；固定纯色法线沿 +Y，仅反射 X 不应翻转 B 或导致对象被剔除。
    // RGBA8 的 128 解码为 +1/255 而非零；反射 X 会改变这点残余 X 分量。
    // 允许两个量化级，仍能区分旧实现约 80 级的 Y 手性错误。
    for (std::size_t channel = 0; channel < positive.size(); ++channel) {
      const auto delta = static_cast<int>(positive[channel]) - static_cast<int>(negative[channel]);
      if (delta < -2 || delta > 2) {
        return 2;
      }
    }
    return 0;
  }
  std::fprintf(stderr, "asset-pixel: pending resources\n");
  run_asset_reload();
  run_pending_resources();
  std::fprintf(stderr, "asset-pixel: mip\n");
  run_mip_sampling();
  std::fprintf(stderr, "asset-pixel: nonzero variant payload\n");
  run_mip_sampling(true);
  std::fprintf(stderr, "asset-pixel: selected variant async\n");
  run_mip_sampling(true, true);
  std::fprintf(stderr, "asset-pixel: channels\n");
  run_material_channels();
  std::fprintf(stderr, "asset-pixel: states\n");
  run_material_states();
  run(gneiss::tasks::execution_mode::thread_pool, true);
  run(gneiss::tasks::execution_mode::cooperative, true);
  run_scene(gneiss::tasks::execution_mode::thread_pool, true);
  run_scene(gneiss::tasks::execution_mode::cooperative, true);
  run(gneiss::tasks::execution_mode::thread_pool);
  run(gneiss::tasks::execution_mode::cooperative);
  run_scene(gneiss::tasks::execution_mode::thread_pool);
  run_scene(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
