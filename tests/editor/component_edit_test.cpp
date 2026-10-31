// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/movement_component.hpp"
#include "../support/value_result.hpp"
#include "commands/author_property_edit.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "panels/reflected_property_editor.hpp"
#include "runtime/application.hpp"
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <source_location>
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
using namespace gneiss::test_support;
using p = e::meta::property_id;
namespace {
int editor_preparations{};
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok)
    throw std::runtime_error("组件编辑测试失败，行=" + std::to_string(at.line()));
}
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  auto ops = operations();
  ops.prepare_editor = [](e::world&, gneiss::scene_node_id,
                          const movement_description& value) -> e::result<movement_description> {
    ++editor_preparations;
    return value;
  };
  check(e::register_scene_component(ops).ok());
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::runtime::application::create({}, app).ok());
  auto& world = *app->native_world();
  ed::scene_document document;
  check(document.create_empty(*app, &world).ok());
  gneiss::scene_node_id node;
  check(document.create_node("movement", {}, node).ok());
  check(e::install_components({world, node}, {movement_description{}}).ok());
  auto ordinary = document.selected_node()->uuid;
  ed::component_edit_target target{.object_uuid = ordinary,
                                   .component_type = movement_description::type_guid};
  ed::editor_command_history history;
  auto apply = [&](ed::property_edit edit, std::uint64_t serial = 0) {
    return ed::edit_component_description(document, history, world, target, edit, serial);
  };
  auto read = [&]() -> movement_description {
    auto components = document.components(node);
    check(components.has_value());
    check(components->get<movement_description>() != nullptr);
    return *components->get<movement_description>();
  };
  auto components = document.components(node);
  check(components && components->size() == 1);
  auto view = components->begin()->view();
  check(view.has_value());
  auto fields = view->type().properties();
  check(fields && fields->size() == 4 && (*fields)[1].type.is_enum());
  check((*fields)[1].type.enum_entries()->size() == 2);
  check(apply({.path = {p{1}}, .data = e::value{3.0F}}, 1).ok());
  check(apply({.path = {p{1}}, .data = e::value{4.0F}}, 1).ok());
  check(history.size() == 1 && read().speed == 4 && document.is_dirty());
  check(history.undo().ok() && read().speed == 1);
  check(history.redo().ok() && read().speed == 4);
  check(editor_preparations >= 4);
  check(apply({.path = {p{2}}, .data = e::value{std::int64_t{1}}}).ok());
  check(read().mode == movement_mode::run);
  check(apply({.path = {p{3}, p{1}}, .data = e::value{6.0F}}).ok());
  check(read().settings.acceleration == 6);
  check(apply({.path = {p{4}, std::size_t{1}}, .data = e::value{8.0F}}).ok());
  check(read().samples[1] == 8);
  auto size = history.size();
  check(apply({.path = {p{4}, std::size_t{99}}, .data = e::value{9.0F}}).failed());
  check(apply({.path = {p{1}}, .data = e::value{-1.0F}}) == e::error::invalid_argument);
  check(history.size() == size && read().speed == 4);
  // Prefab 使用同一个注册组件和编辑命令，不写专用分支。
  auto prefab = std::make_shared<e::prefab_description>();
  prefab->uuid = e::guid::from_bytes({85, 1});
  prefab->root_uuid = e::guid::from_bytes({85, 2});
  e::object_description root;
  root.uuid = prefab->root_uuid;
  root.components.set(movement_description{});
  prefab->objects.push_back(root);
  e::object_description reference;
  reference.uuid = e::guid::from_bytes({85, 3});
  reference.prefab.emplace();
  reference.prefab->uri = "asset://movement.prefab";
  const std::array<e::scene_mesh_binding, 1> bindings{};
  check(gneiss::test_support::take_value(
            app->scenes()->instantiate_prefab_ready(document.scene_handle(), reference,
                                                    e::prefab_asset_ref{prefab}, bindings, false),
            node)
            .ok());
  check(document.synchronize_nodes().ok());
  check(history.clear().ok());
  target = {.instance_uuid = reference.uuid,
            .source_uuid = root.uuid,
            .component_type = movement_description::type_guid};
  check(apply({.path = {p{3}, p{1}}, .data = e::value{7.0F}}).ok());
  check(apply({.path = {p{4}, std::size_t{1}}, .data = e::value{9.0F}}).ok());
  check(read().settings.acceleration == 7 && read().samples[1] == 9);
  check(prefab->objects[0].components.get<movement_description>()->samples[1] == 2);
  auto snapshot = [&] {
    e::scene_description output;
    check(gneiss::test_support::assign_value(
              app->scenes()->capture_description(document.scene_handle()), output)
              .ok());
    auto bytes = e::encode_scene(output);
    check(bytes.has_value());
    auto decoded = e::decode_scene(*bytes);
    check(decoded.has_value());
    return *decoded;
  };
  auto override_count = [&] {
    auto saved = snapshot();
    for (auto& object : saved.objects)
      if (object.prefab)
        return object.prefab->overrides.size();
    throw std::runtime_error("找不到 Prefab");
  };
  check(override_count() == 2);
  check(history.undo().ok() && override_count() == 1 && read().samples[1] == 2);
  check(history.redo().ok() && override_count() == 2);
  // 实际绘制反射控件并点击序列元素的 Restore Source，控件只产生操作值。
  auto current = document.components(node);
  auto current_view = current->begin()->view();
  auto source_view = prefab->objects[0].components.begin()->view();
  check(current_view && source_view);
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1200, 900};
  io.DeltaTime = 1.0F / 60;
  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  e::error status = e::error::success;
  std::uint64_t serial = 1;
  std::optional<ed::property_edit> requested;
  ImVec2 button;
  const auto draw = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({1100, 850});
    ImGui::Begin("Components");
    auto pending =
        ed::draw_reflected_properties(*current_view, "Movement", serial, status, {}, &*source_view);
    if (pending)
      requested = std::move(pending);
    button = GImGui->LastItemData.Rect.GetCenter();
    ImGui::End();
    ImGui::Render();
  };
  draw();
  draw();
  check(status.ok() && ImGui::GetDrawData()->TotalVtxCount > 0);
  io.AddMousePosEvent(button.x, button.y);
  io.AddMouseButtonEvent(0, true);
  draw();
  io.AddMouseButtonEvent(0, false);
  draw();
  check(requested && requested->path == ed::property_path{p{4}, std::size_t{1}});
  check(read().samples[1] == 9);
  check(apply(*requested).ok() && read().samples[1] == 2 && override_count() == 1);
  check(history.undo().ok() && read().samples[1] == 9 && override_count() == 2);
  ImGui::DestroyContext();
  // 同时恢复嵌套对象多个叶字段，仍通过同一命令和稀疏覆盖。
  check(apply({.path = {p{3}},
               .object_type = e::runtime_type_of<movement_settings>(),
               .object = gneiss::engine::make_ref<movement_settings>()})
            .ok());
  check(read().settings.acceleration == 1 && override_count() == 1);
  check(history.undo().ok() && read().settings.acceleration == 7);
  // 序列结构变更不属于本阶段覆盖语义，拒绝并保留当前值。
  auto replacement = read();
  replacement.samples.push_back(3);
  check(apply({.path = {},
               .object_type = e::runtime_type_of<movement_description>(),
               .object = gneiss::engine::make_ref<movement_description>(replacement)}) ==
        e::error::unsupported);
  check(read().samples.size() == 2);
  // 两个相互约束的字段必须整体覆盖，不能按路径顺序提前校验中间态。
  check(apply({.path = {p{3}},
               .object_type = e::runtime_type_of<movement_settings>(),
               .object = gneiss::engine::make_ref<movement_settings>(movement_settings{20, 30})})
            .ok());
  auto invalid = snapshot();
  auto invalid_prefab = std::ranges::find_if(
      invalid.objects, [](const auto& value) { return value.prefab.has_value(); });
  check(invalid_prefab != invalid.objects.end());
  invalid_prefab->prefab->overrides.front().key.path = {p{0}};
  check(!e::encode_scene(invalid));
  auto valid_bytes = e::encode_scene(snapshot());
  check(valid_bytes.has_value());
  std::string malformed(reinterpret_cast<const char*>(valid_bytes->data()), valid_bytes->size());
  auto path_start = malformed.find("\"path\"");
  check(path_start != std::string::npos);
  auto array_start = malformed.find('[', path_start), array_end = malformed.find(']', array_start);
  malformed.replace(array_start, array_end - array_start + 1, R"([{"index":-1}])");
  auto rejected = e::decode_scene(std::as_bytes(std::span{malformed.data(), malformed.size()}));
  check(!rejected && rejected.error().path.find("/path") != std::string::npos);
  e::scene_description_inputs inputs;
  inputs.scene = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(snapshot())};
  inputs.prefabs.emplace(reference.prefab->uri, e::prefab_asset_ref{prefab});
  e::scene_instantiation_data prepared;
  e::scene_diagnostic diagnostic;
  check(e::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {}).ok());
  gneiss::scene_instance_id loaded;
  check(gneiss::test_support::take_value(
            app->scenes()->instantiate_prepared(std::move(prepared), {}), loaded)
            .ok());
  gneiss::scene_node_id loaded_node;
  check(gneiss::test_support::take_value(app->scenes()->find_node(loaded, reference.uuid),
                                         loaded_node)
            .ok());
  auto reloaded = e::capture_components(world, loaded_node);
  check(reloaded && reloaded->get<movement_description>()->settings.acceleration == 20 &&
        reloaded->get<movement_description>()->samples[1] == 9);
  check(gneiss::test_support::take_value(app->scenes()->find_node(loaded, ordinary), loaded_node)
            .ok());
  reloaded = e::capture_components(world, loaded_node);
  check(reloaded && reloaded->get<movement_description>()->mode == movement_mode::run &&
        reloaded->get<movement_description>()->speed == 4);
  check(app->scenes()->unload(loaded).ok());
  check(app->scenes()->remove_prefab_instance(document.scene_handle(), reference.uuid).ok());
  check(document.synchronize_nodes().ok());
  check(history.undo() == e::error::not_found);
  return 0;
} catch (const std::exception& failure) {
  std::fprintf(stderr, "%s\n", failure.what());
  return 1;
}
