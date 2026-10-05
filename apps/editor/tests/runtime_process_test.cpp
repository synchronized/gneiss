// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_log_adapter.hpp"
#include "runtime_process.hpp"
#include "runtime_property_adapter.hpp"
#include "runtime_scene_adapter.hpp"

#include <gneiss/world.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

namespace {

struct temporary_project final {
  std::filesystem::path root;

  ~temporary_project() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};

bool test_scene_adapter() {
  gneiss::editor::runtime_scene_batch output;
  {
    gneiss::ipc_inspection_batch input{
        .stamp = {31U, 32U}, .is_full = true, .chunk_index = 2U, .chunk_count = 3U, .changes = {}};
    gneiss::ipc_inspection_change change;
    change.id = {41U, 42U};
    change.node.id = change.id;
    change.node.parent = {43U, 44U};
    change.node.uuid = "node-uuid";
    change.node.prefab_instance_uuid = "instance-uuid";
    change.node.prefab_source_node_uuid = "source-uuid";
    change.node.name = std::string(512U, 's');
    change.node.local_transform.translation[1] = 7.0F;
    change.node.component_flags = 3U;
    change.node.camera.near_plane = 0.75F;
    change.node.mesh_uri = "asset://mesh";
    change.node.material_uri = "asset://material";
    input.changes.push_back(std::move(change));
    input.changes.push_back(
        {.type = gneiss::ipc_inspection_change_type::remove, .id = {51U, 52U}, .node = {}});
    if (gneiss::editor::to_runtime_scene_batch(std::move(input), output) !=
        gneiss::result::success) {
      return false;
    }
  }
  if (output.stamp.session_id != 31U || output.stamp.sequence != 32U || !output.is_full ||
      output.chunk_index != 2U || output.chunk_count != 3U || output.changes.size() != 2U) {
    return false;
  }
  const auto& change = output.changes[0];
  const auto& node = change.node;
  if (change.type != gneiss::editor::runtime_scene_change_type::upsert || change.id.value != 41U ||
      change.id.generation != 42U || node.id != change.id || node.parent.value != 43U ||
      node.parent.generation != 44U || node.uuid != "node-uuid" ||
      node.prefab_instance_uuid != "instance-uuid" ||
      node.prefab_source_node_uuid != "source-uuid" || node.name != std::string(512U, 's') ||
      node.local_transform.translation[1] != 7.0F || node.component_flags != 3U ||
      node.camera.near_plane != 0.75F || node.mesh_uri != "asset://mesh" ||
      node.material_uri != "asset://material" ||
      output.changes[1].type != gneiss::editor::runtime_scene_change_type::remove ||
      output.changes[1].id.value != 51U || output.changes[1].id.generation != 52U) {
    return false;
  }
  gneiss::ipc_inspection_batch invalid;
  invalid.changes.push_back(
      {.type = static_cast<gneiss::ipc_inspection_change_type>(255U), .id = {}, .node = {}});
  return gneiss::editor::to_runtime_scene_batch(std::move(invalid), output) ==
             gneiss::result::invalid_argument &&
         output.stamp.session_id == 31U && output.changes.size() == 2U &&
         output.changes[0].node.name == std::string(512U, 's');
}

bool test_property_adapter() {
  const std::array<gneiss::ipc_property_payload, 10> values{
      std::monostate{},
      true,
      std::int64_t{-17},
      std::uint64_t{42},
      1.5F,
      2.5,
      std::string(512U, 'p'),
      std::array<std::uint8_t, 16>{1U, 2U},
      std::array<float, 3>{1.0F, 2.0F, 3.0F},
      std::array<float, 4>{1.0F, 2.0F, 3.0F, 4.0F}};
  for (const auto& payload : values) {
    gneiss::editor::runtime_property_write request{
        .session_id = 11U,
        .command_id = 12U,
        .object = {13U, 14U},
        .type_id = {{15U}},
        .field_id = 16U,
        .expected_revision = 17U,
        .value = gneiss::editor::to_runtime_property_value({payload})};
    const auto wire = gneiss::editor::to_ipc_property_write(std::move(request));
    if (wire.session_id != 11U || wire.command_id != 12U || wire.object.value != 13U ||
        wire.object.generation != 14U || wire.type_id.bytes[0] != 15U || wire.field_id != 16U ||
        wire.expected_revision != 17U || wire.value.payload != payload) {
      return false;
    }
    // 临时协议响应在表达式结束时销毁，模型结果必须持有消息与规范值。
    const auto result =
        gneiss::editor::to_runtime_property_result({.session_id = 11U,
                                                    .command_id = 12U,
                                                    .code = GNEISS_ERROR_INVALID_ARGUMENT,
                                                    .revision = 18U,
                                                    .message = std::string(256U, 'r'),
                                                    .canonical_value = {payload}});
    if (result.session_id != 11U || result.command_id != 12U ||
        result.code != GNEISS_ERROR_INVALID_ARGUMENT || result.revision != 18U ||
        result.message != std::string(256U, 'r') || result.canonical_value.payload != payload) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() try {
  if (!test_scene_adapter() || !test_property_adapter()) {
    return 91;
  }
  // 协议对象销毁后，控制台值仍拥有全部字段和长字符串。
  auto converted = [] {
    gneiss::app::runtime_log_record record;
    record.severity = GNEISS_LOG_ERROR;
    record.sequence = 42U;
    record.timestamp_ns = 123456U;
    record.thread_id = 17U;
    record.source = "runtime-module";
    record.category = "resource-import";
    record.message = std::string(512U, 'x');
    record.operation = GNEISS_ERROR_NOT_FOUND;
    return gneiss::editor::to_console_event(std::move(record));
  }();
  if (converted.severity != GNEISS_LOG_ERROR || converted.sequence != 42U ||
      converted.timestamp_ns != 123456U || converted.thread_id != 17U ||
      converted.source != "runtime-module" || converted.category != "resource-import" ||
      converted.message != std::string(512U, 'x') ||
      converted.operation != GNEISS_ERROR_NOT_FOUND) {
    return 90;
  }

  temporary_project reload_project{
      std::filesystem::temp_directory_path() / "Gneiss" /
          ("runtime-reload-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
  };
  std::filesystem::create_directories(reload_project.root);
  std::filesystem::copy(GNEISS_TEST_PROJECT_ROOT, reload_project.root,
                        std::filesystem::copy_options::recursive);
  gneiss::editor::runtime_process process;
  if (process.retry_asset_reload() != gneiss::result::not_ready) {
    return 16;
  }
  const std::vector<std::string> over_capacity(1025U, "asset://test.texture.json");
  if (process.publish_asset_revision(over_capacity) != gneiss::result::not_ready) {
    return 26;
  }
  const std::vector<std::string> over_texture_batch(17U, "asset://test.texture.json");
  if (process.publish_asset_revision(over_texture_batch) != gneiss::result::invalid_argument) {
    return 27;
  }
  gneiss::editor::runtime_launch_request request{reload_project.root};
  const std::filesystem::path executable{GNEISS_TEST_RUNTIME};
  const auto missing_executable = executable.parent_path() / "missing-runtime";
  if (process.start(missing_executable, request) != gneiss::result::not_found) {
    return 1;
  }

  temporary_project invalid_project{std::filesystem::temp_directory_path() / "Gneiss" /
                                    "runtime-process-invalid-project"};
  std::error_code error;
  std::filesystem::remove_all(invalid_project.root, error);
  std::filesystem::create_directories(invalid_project.root / "assets", error);
  std::ofstream project_file(invalid_project.root / "gneiss.project.json",
                             std::ios::binary | std::ios::trunc);
  project_file << R"({
  "format": "gneiss.project",
  "version": 1,
  "name": "Invalid Runtime Project",
  "asset_root": "assets",
  "startup_scene": "asset://scenes/missing.scene.json"
})";
  project_file.close();
  if (error || !project_file) {
    return 2;
  }
  gneiss::editor::runtime_launch_request invalid_request{invalid_project.root};
  if (process.start(executable, invalid_request) != gneiss::result::success) {
    return 3;
  }
  const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.is_running() && std::chrono::steady_clock::now() < failure_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  const auto failure_log = process.log_file();
  if (process.is_running() || process.exit_code() != 2 ||
      process.output().find("stage=startup_scene") == std::string::npos ||
      process.output().find("missing.scene.json") == std::string::npos ||
      !std::filesystem::is_regular_file(failure_log)) {
    return 4;
  }

  if (process.start(executable, request) != gneiss::result::success || !process.is_running() ||
      process.start(executable, request) != gneiss::result::invalid_state) {
    return 5;
  }
  if (!std::filesystem::is_regular_file(failure_log)) {
    return 6;
  }

  const auto startup_started = std::chrono::steady_clock::now();
  const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < startup_deadline &&
         (process.output().find("Runtime 已进入首帧") == std::string::npos ||
          process.control_state() != gneiss::editor::runtime_control_state::running)) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  const auto current_session = process.console().current_session_id();
  const auto has_structured_event = [&] {
    return std::ranges::any_of(process.console().entries(), [current_session](const auto& entry) {
      return entry.session_id == current_session &&
             entry.kind == gneiss::editor::console_entry_kind::structured;
    });
  };
  while (std::chrono::steady_clock::now() < startup_deadline && !has_structured_event()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  while (std::chrono::steady_clock::now() < startup_deadline &&
         process.scene_mirror().nodes().empty()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  while (std::chrono::steady_clock::now() < startup_deadline &&
         process.statistics().sequence == 0U) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!process.is_running() || process.output().find("Runtime 已进入首帧") == std::string::npos ||
      !has_structured_event() || process.scene_mirror().needs_full_snapshot() ||
      process.scene_mirror().nodes().empty() ||
      process.statistics().session_id != process.scene_mirror().session_id() ||
      process.statistics().scene_node_count == 0U || process.statistics().entity_count == 0U ||
      process.control_state() != gneiss::editor::runtime_control_state::running) {
    return 7;
  }
  const std::array<std::string, 1> unsupported_reload{"asset://unknown.bin"};
  if (process.publish_asset_revision({}) != gneiss::result::invalid_argument ||
      process.asset_reload_status().publish_result != gneiss::result::invalid_argument ||
      process.publish_asset_revision(unsupported_reload) != gneiss::result::unsupported ||
      process.asset_reload_status().publish_result != gneiss::result::unsupported) {
    return 21;
  }
  const std::array<std::string, 3> mixed_reload{"asset://materials/triangle.material.json",
                                                "asset://models/triangle.mesh.json",
                                                "asset://scenes/main.scene.json"};
  if (process.publish_asset_revision(mixed_reload) != gneiss::result::success ||
      process.asset_reload_status().publish_result != gneiss::result::success) {
    return 7;
  }
  const auto reload_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (process.asset_reload_status().state !=
             gneiss::editor::runtime_asset_reload_state::applied &&
         std::chrono::steady_clock::now() < reload_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (process.asset_reload_status().state != gneiss::editor::runtime_asset_reload_state::applied ||
      process.asset_reload_status().revision != 2U ||
      process.request_pause() != gneiss::result::success) {
    return 7;
  }
  // 非法材质报告失败；修复磁盘文件后显式重同步，无需重启进程。
  const auto material_path = reload_project.root / "assets/materials/triangle.material.json";
  {
    std::ofstream broken_material(material_path, std::ios::trunc);
    broken_material << "invalid material";
  }
  const std::array<std::string, 1> material_reload{mixed_reload.front()};
  if (process.publish_asset_revision(material_reload) != gneiss::result::success) {
    return 17;
  }
  const auto wait_for_reload = [&](gneiss::editor::runtime_asset_reload_state expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (process.asset_reload_status().state != expected &&
           std::chrono::steady_clock::now() < deadline) {
      process.update();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return process.asset_reload_status().state == expected;
  };
  if (!wait_for_reload(gneiss::editor::runtime_asset_reload_state::failed) ||
      process.asset_reload_status().message.empty()) {
    return 18;
  }
  const auto failed_revision = process.asset_reload_status().revision;
  // 未修复时重试仍失败，重复点击不得重复排队。
  if (process.retry_asset_reload() != gneiss::result::success ||
      process.retry_asset_reload() != gneiss::result::not_ready ||
      !wait_for_reload(gneiss::editor::runtime_asset_reload_state::failed)) {
    return 19;
  }
  std::filesystem::copy_file(std::filesystem::path{GNEISS_TEST_PROJECT_ROOT} /
                                 "assets/materials/triangle.material.json",
                             material_path, std::filesystem::copy_options::overwrite_existing);
  if (process.retry_asset_reload() != gneiss::result::success ||
      !wait_for_reload(gneiss::editor::runtime_asset_reload_state::applied) ||
      process.asset_reload_status().revision <= failed_revision ||
      process.retry_asset_reload() != gneiss::result::not_ready || !process.is_running()) {
    return 20;
  }
  // 实际 Runtime 的纯纹理异步回执：准备失败后恢复，窗口与 IPC 持续运行。
  const auto texture_root = reload_project.root / "assets/textures";
  std::filesystem::create_directories(texture_root);
  constexpr std::array<unsigned char, 68> png{
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0,    0,    0,    0x0D, 0x49, 0x48,
      0x44, 0x52, 0,    0,    0,    1,    0,    0,    0,    1,    8,    4,    0,    0,
      0,    0xB5, 0x1C, 0x0C, 2,    0,    0,    0,    0x0B, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xDA, 0x63, 0x64, 0xF8, 0x0F, 0,    1,    5,    1,    1,    0x27, 0x18, 0xE3, 0x66,
      0,    0,    0,    0,    0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
  const auto write_png = [&] {
    std::ofstream stream(texture_root / "test.png", std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
  };
  write_png();
  {
    std::ofstream stream(texture_root / "test.texture.json");
    stream
        << R"({"format":"gneiss.texture","version":1,"source":"asset://textures/test.png","color_space":"srgb"})";
  }
  {
    std::ofstream stream(material_path);
    stream
        << R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://textures/test.texture.json","metallic":0,"roughness":1})";
  }
  if (process.publish_asset_revision(material_reload) != gneiss::result::success ||
      !wait_for_reload(gneiss::editor::runtime_asset_reload_state::applied)) {
    return 22;
  }
  const std::array<std::string, 1> texture_reload{"asset://textures/test.texture.json"};
  if (process.publish_asset_revision(texture_reload) != gneiss::result::success ||
      !wait_for_reload(gneiss::editor::runtime_asset_reload_state::applied)) {
    return 23;
  }
  {
    std::ofstream broken(texture_root / "test.png", std::ios::trunc);
    broken << "invalid";
  }
  if (process.publish_asset_revision(texture_reload) != gneiss::result::success ||
      !wait_for_reload(gneiss::editor::runtime_asset_reload_state::failed)) {
    return 24;
  }
  write_png();
  if (process.retry_asset_reload() != gneiss::result::success ||
      !wait_for_reload(gneiss::editor::runtime_asset_reload_state::applied) ||
      !process.is_running()) {
    return 25;
  }
  gneiss::editor::runtime_property_key property_key{
      .object = {process.scene_mirror().nodes().front().id.value,
                 process.scene_mirror().nodes().front().id.generation},
      .type_id = {},
      .field_id = GNEISS_TRANSFORM_FIELD_TRANSLATION};
  const auto transform_type = gneiss_transform_type_id();
  std::ranges::copy(transform_type.bytes, property_key.type_id.begin());
  if (!process.supports_property_editing() ||
      process.request_property_write(property_key, 1U,
                                     {std::array<float, 3>{0.25F, 0.5F, 0.75F}}) !=
          gneiss::result::success ||
      process.request_property_write(property_key, 1U, {true}) != gneiss::result::not_ready) {
    return 7;
  }
  const auto property_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  const gneiss::editor::runtime_property_edit* property_edit = nullptr;
  while (std::chrono::steady_clock::now() < property_deadline) {
    process.update();
    property_edit = process.property_edit(property_key);
    if (property_edit != nullptr &&
        property_edit->state != gneiss::editor::runtime_property_edit_state::pending) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (property_edit == nullptr ||
      property_edit->state != gneiss::editor::runtime_property_edit_state::applied ||
      property_edit->revision != 2U) {
    return 7;
  }
  const auto lifecycle_event_count =
      std::ranges::count_if(process.console().entries(), [current_session](const auto& entry) {
        return entry.session_id == current_session &&
               entry.kind == gneiss::editor::console_entry_kind::structured &&
               entry.event.category == "lifecycle" && entry.event.message == "Runtime 已进入首帧";
      });
  if (lifecycle_event_count != 1) {
    return 7;
  }
  const auto pause_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (process.control_state() != gneiss::editor::runtime_control_state::paused &&
         std::chrono::steady_clock::now() < pause_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (process.control_state() != gneiss::editor::runtime_control_state::paused ||
      !process.supports_property_editing()) {
    return 7;
  }
  auto paused_key = property_key;
  paused_key.field_id = GNEISS_TRANSFORM_FIELD_SCALE;
  if (process.request_property_write(paused_key, 1U, {std::array<float, 3>{1.1F, 1.2F, 1.3F}}) !=
      gneiss::result::success) {
    return 7;
  }
  const auto paused_edit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  const gneiss::editor::runtime_property_edit* paused_edit = nullptr;
  while (std::chrono::steady_clock::now() < paused_edit_deadline) {
    process.update();
    paused_edit = process.property_edit(paused_key);
    if (paused_edit != nullptr &&
        paused_edit->state != gneiss::editor::runtime_property_edit_state::pending) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (paused_edit == nullptr ||
      paused_edit->state != gneiss::editor::runtime_property_edit_state::applied ||
      process.request_resume() != gneiss::result::success) {
    return 7;
  }
  const auto resume_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (process.control_state() != gneiss::editor::runtime_control_state::running &&
         std::chrono::steady_clock::now() < resume_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (process.control_state() != gneiss::editor::runtime_control_state::running ||
      process.request_stop() != gneiss::result::success) {
    return 7;
  }
  const auto startup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - startup_started)
                              .count();
  std::printf("runtime_startup_to_first_frame_ms=%lld\n", static_cast<long long>(startup_ms));

  const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < stop_deadline && process.is_running()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  const auto first_inspection_session = process.scene_mirror().session_id();
  if (process.is_running() || process.exit_code() != 0 || !process.received_shutdown_complete() ||
      process.output().find("收到 Editor IPC 停止请求") == std::string::npos ||
      process.output().find("stage=shutdown") == std::string::npos ||
      process.request_stop() != gneiss::result::not_ready) {
    return 8;
  }

  if (process.start(executable, request) != gneiss::result::success) {
    return 9;
  }
  const auto replay_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < replay_deadline &&
         (process.control_state() != gneiss::editor::runtime_control_state::running ||
          process.scene_mirror().nodes().empty() ||
          process.scene_mirror().session_id() == first_inspection_session ||
          process.asset_reload_status().state !=
              gneiss::editor::runtime_asset_reload_state::applied)) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!process.is_running() || process.scene_mirror().nodes().empty() ||
      process.scene_mirror().session_id() == first_inspection_session ||
      process.asset_reload_status().state != gneiss::editor::runtime_asset_reload_state::applied ||
      process.request_stop() != gneiss::result::success) {
    return 9;
  }
  while (process.is_running() && std::chrono::steady_clock::now() < replay_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  process.update();
  if (process.is_running() || process.exit_code() != 0 || !process.received_shutdown_complete()) {
    return 9;
  }

  if (process.start(GNEISS_TEST_CHILD_PROCESS, request) != gneiss::result::success ||
      process.request_stop() != gneiss::result::success) {
    return 10;
  }
  const auto forced_stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (process.is_running() && std::chrono::steady_clock::now() < forced_stop_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_running() || process.exit_code() == 0 ||
      process.output().find("已强制终止") == std::string::npos) {
    return 11;
  }

  gneiss::app::project_description module_project;
  module_project.project_root = request.project_root;
  module_project.game_module.name = "test_game";
  module_project.game_module.profiles[0].directory = "modules";
  module_project.game_module.profiles[0].configure_preset = "game-debug-configure";
  module_project.game_module.profiles[0].build_preset = "game-debug";
  module_project.game_module.build_target = "build-fail";
  if (process.build_and_start(GNEISS_TEST_CHILD_PROCESS, executable, request, module_project) !=
          gneiss::result::success ||
      !process.is_building()) {
    return 12;
  }
  const auto build_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.is_building() && std::chrono::steady_clock::now() < build_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_busy() || process.last_result() != gneiss::result::dependency_failed ||
      process.output().find("fixture build failed") == std::string::npos ||
      process.output().find("未启动 Runtime") == std::string::npos) {
    return 13;
  }

  std::filesystem::remove_all(invalid_project.root / "assets", error);
  std::filesystem::copy(std::filesystem::path{GNEISS_TEST_PROJECT_ROOT} / "assets",
                        invalid_project.root / "assets", std::filesystem::copy_options::recursive,
                        error);
  project_file.open(invalid_project.root / "gneiss.project.json",
                    std::ios::binary | std::ios::trunc);
  project_file << R"({
  "format": "gneiss.project",
  "version": 1,
  "name": "Build Success Project",
  "asset_root": "assets",
  "startup_scene": "asset://scenes/main.scene.json"
})";
  project_file.close();
  module_project.project_root = invalid_project.root;
  module_project.game_module.build_target = "build-success";
  invalid_request.project_root = invalid_project.root;
  if (error || !project_file ||
      process.build_and_start(GNEISS_TEST_CHILD_PROCESS, executable, invalid_request,
                              module_project) != gneiss::result::success) {
    return 14;
  }
  const auto build_success_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < build_success_deadline &&
         process.output().find("Runtime 已进入首帧") == std::string::npos && process.is_busy()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!process.is_running() || process.output().find("游戏模块构建完成") == std::string::npos ||
      process.output().find("Runtime 已进入首帧") == std::string::npos ||
      process.request_stop() != gneiss::result::success) {
    return 15;
  }
  return 0;
} catch (...) {
  return 99;
}
