// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/serialization.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/platform/files/memory_file_backend.hpp"
#include "engine/resource/material/material_codec.hpp"
#include "panels/material_editor_panel.hpp"
#include <cstdio>
#include <imgui.h>
#include <stdexcept>
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
void check(bool good, const char* message) {
  if (!good)
    throw std::runtime_error(message);
}
std::string text(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
int main() try {
  check(e::initialize_meta().ok(), "initialize");
  e::files::service files;
  auto backend = std::make_shared<e::files::memory_backend>();
  check(files.mount({.prefix = "asset://", .readers = {backend}, .writer = backend}).ok(), "mount");
  auto encoded = e::encode_material(e::material_description{});
  check(bool(encoded), "encode fixture");
  const auto original = text(*encoded);
  const std::string uri = "asset://materials/test.material.json";
  auto write = [&](const std::string& content) {
    bool done = false;
    e::error result = e::error::not_ready;
    e::files::request_id id;
    const auto bytes = std::as_bytes(std::span{content.data(), content.size()});
    check(files
              .write(
                  uri, {bytes.begin(), bytes.end()}, {},
                  [&](e::error status) {
                    done = true;
                    result = status;
                  },
                  id)
              .ok(),
          "write accepted");
    check(files.dispatch_completions(64).ok() && done && result.ok(), "write complete");
  };
  auto read = [&] {
    bool done = false;
    std::string output;
    e::files::request_id id;
    check(files
              .read(
                  uri, {16U * 1024U * 1024U},
                  [&](e::files::completion<e::files::bytes> completion) {
                    check(completion.status.ok(), "read success");
                    output = text(completion.data);
                    done = true;
                  },
                  id)
              .ok(),
          "read accepted");
    check(files.dispatch_completions(64).ok() && done, "read complete");
    return output;
  };
  write(original);
  ed::material_editor_model model;
  check(model.open(uri, original).ok() && !model.is_dirty(), "open material");
  using p = e::meta::property_id;
  const ed::material_property_path rough{p{1}, p{3}};
  {
    auto parameters = model.description().parameters;
    parameters.roughness = 0.6F;
    auto source = e::meta::instance::from(parameters);
    check(bool(source) && model.replace_object({p{1}}, *source).ok(), "replace object");
    check(model.description().parameters.roughness == 0.6F && parameters.roughness == 0.6F,
          "copy does not consume source");
    check(model.undo().ok() && !model.is_dirty(), "replacement undo");
    check(model.redo().ok() && model.description().parameters.roughness == 0.6F,
          "replacement redo");
    parameters.roughness = -1;
    check(model.replace_object({p{1}}, *source).failed() &&
              model.description().parameters.roughness == 0.6F,
          "invalid replacement atomic");
    auto wrong = e::meta::instance::from(model.description());
    check(model.replace_object({p{1}}, *wrong) == e::error::invalid_argument, "wrong type");
    check(model.replace_object({p{1}, p{3}}, *source) == e::error::unsupported,
          "scalar replacement rejected");
    auto defaults = e::meta::deserialize<e::material_parameters>(
        std::as_bytes(std::span{"{}", std::size_t{2}}));
    check(bool(defaults), "decode defaults");
    auto default_view = e::meta::instance::from(*defaults);
    check(model.replace_object({p{1}}, *default_view).ok() && !model.is_dirty(),
          "reset object defaults");
    check(model.undo().ok() && model.description().parameters.roughness == 0.6F, "reset undo");
    e::texture_sampling sampling;
    sampling.uv_set = 1;
    auto sampling_view = e::meta::instance::from(sampling);
    check(model.replace_object({p{1}, p{10}, std::size_t{2}}, *sampling_view).ok() &&
              model.description().parameters.sampling[2].uv_set == 1,
          "replace array object");
    check(model.discard().ok(), "restore fixture");
    auto metadata = e::meta::describe_type(e::runtime_type_of<e::material_parameters>());
    check(bool(metadata) && metadata->properties[6].choices.size() == 3 &&
              metadata->properties[6].choices[2].name == "blend",
          "owned enum metadata");
  }
  const auto initial = model.description().parameters.roughness;
  check(model.set_value(rough, e::value{0.2F}, 7).ok() &&
            model.set_value(rough, e::value{0.4F}, 7).ok(),
        "merged edits");
  check(model.undo().ok() && model.description().parameters.roughness == initial &&
            !model.is_dirty() && !model.can_undo(),
        "single undo restores original");
  check(model.redo().ok() && model.description().parameters.roughness == 0.4F, "redo");
  check(model.set_value(rough, e::value{-1.0F}).failed() &&
            model.description().parameters.roughness == 0.4F,
        "invalid edit is atomic");
  check(model.set_value({p{2}, std::size_t{99}}, e::value{std::string{}}).failed(), "bad index");
  check(model.set_value({p{1}, p{10}, std::size_t{0}, p{1}}, e::value{std::uint64_t{1}}).ok(),
        "nested array");
  check(model.set_value({p{1}, p{1}}, e::value{e::math::vector4{0.2F, 0.3F, 0.4F, 1}}).ok(),
        "color");
  check(model
            .set_value({p{2}, std::size_t{0}},
                       e::value{std::string{"asset://textures/a.texture.json"}})
            .ok(),
        "texture URI");
  check(model.set_value({p{2}, std::size_t{0}}, e::value{std::string{"../outside"}}).failed(),
        "invalid URI");
  check(model.open("asset://other", original).failed(), "dirty switch rejected");
  check(model.begin_save(files).ok() && model.saving(), "save submitted");
  check(model.discard().failed(), "inflight discard rejected");
  check(model.set_value(rough, e::value{0.8F}).ok(), "edit while saving");
  for (int i = 0; i < 8 && model.saving(); ++i) {
    check(files.dispatch_completions(64).ok(), "dispatch");
    model.advance();
  }
  check(!model.saving() && model.save_status().ok() && model.is_dirty(),
        "save does not clear newer edits");
  auto disk = read();
  auto disk_material = e::decode_material(std::as_bytes(std::span{disk.data(), disk.size()}));
  check(disk_material && disk_material->parameters.roughness == 0.4F &&
            disk_material->texture_uris[0] == "asset://textures/a.texture.json",
        "saved snapshot content");
  check(model.undo().ok() && !model.is_dirty(), "undo to saved snapshot");
  check(model.redo().ok() && model.is_dirty(), "redo newer edit");
  write(original);
  check(model.begin_save(files).ok(), "conflict save accepted");
  for (int i = 0; i < 8 && model.saving(); ++i) {
    check(files.dispatch_completions(64).ok(), "dispatch");
    model.advance();
  }
  check(model.save_status() == e::error::invalid_state && model.is_dirty(),
        "external modification preserved");
  check(read() == original, "conflicting file was not overwritten");
  check(model.discard().ok() && !model.is_dirty(), "discard");
  check(model.open(uri, original).ok(), "reload");
  // 无窗口、无 GPU 的面板绘制，覆盖所有材质叶子与固定数组。
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1024, 768};
  io.DeltaTime = 1.0F / 60;
  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  bool visible = true, focused = false;
  std::uint64_t serial = 0;
  e::error status = e::error::success;
  ImGui::NewFrame();
  ImGui::SetNextWindowSize({700, 700});
  ed::draw_material_editor(model, files, visible, serial, status, focused);
  ImGui::Render();
  check(status.ok() && ImGui::GetDrawData()->TotalVtxCount > 0 && !model.is_dirty(),
        "draw reflected tree");
  ImGui::DestroyContext();
  // 销毁模型后，保存回调只访问独立回执。
  {
    ed::material_editor_model temporary;
    check(temporary.open(uri, original).ok(), "temporary open");
    check(temporary.set_value(rough, e::value{0.6F}).ok(), "temporary edit");
    check(temporary.begin_save(files).ok(), "temporary save");
  }
  for (int i = 0; i < 4; ++i)
    check(files.dispatch_completions(64).ok(), "late callbacks");
  bool stopped = false;
  check(files.shutdown([&](e::error status) { stopped = status.ok(); }).ok(), "shutdown");
  for (int i = 0; i < 8 && !stopped; ++i)
    check(files.dispatch_completions(64).ok(), "shutdown dispatch");
  check(stopped, "shutdown complete");
  return 0;
} catch (const std::exception& error) {
  if (ImGui::GetCurrentContext())
    ImGui::DestroyContext();
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
