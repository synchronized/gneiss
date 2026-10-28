// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "imgui_adapter.hpp"
#include "engine/core/result.hpp"
#include "engine/function/resource/render_resource_factory.hpp"
#include "runtime/application.hpp"

#include "editor_theme.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>

namespace gneiss::editor {
namespace {

constexpr std::uint32_t premultiply_color(std::uint32_t color) noexcept {
  const auto alpha = (color >> 24U) & 0xffU;
  const auto premultiply = [alpha](std::uint32_t channel) {
    return (channel * alpha + 127U) / 255U;
  };
  return (alpha << 24U) | (premultiply((color >> 16U) & 0xffU) << 16U) |
         (premultiply((color >> 8U) & 0xffU) << 8U) | premultiply(color & 0xffU);
}

static_assert(premultiply_color(0x80ffffffU) == 0x80808080U);

ImGuiKey map_key(std::uint32_t key) noexcept {
  if (key >= gneiss::engine::physical_key_a && key <= gneiss::engine::physical_key_z) {
    return static_cast<ImGuiKey>(ImGuiKey_A + key - gneiss::engine::physical_key_a);
  }
  if (key >= gneiss::engine::physical_key_1 && key <= gneiss::engine::physical_key_9) {
    return static_cast<ImGuiKey>(ImGuiKey_1 + key - gneiss::engine::physical_key_1);
  }
  if (key >= gneiss::engine::physical_key_f1 && key <= gneiss::engine::physical_key_f12) {
    return static_cast<ImGuiKey>(ImGuiKey_F1 + key - gneiss::engine::physical_key_f1);
  }
  switch (key) {
  case gneiss::engine::physical_key_0:
    return ImGuiKey_0;
  case gneiss::engine::physical_key_enter:
    return ImGuiKey_Enter;
  case gneiss::engine::physical_key_escape:
    return ImGuiKey_Escape;
  case gneiss::engine::physical_key_backspace:
    return ImGuiKey_Backspace;
  case gneiss::engine::physical_key_tab:
    return ImGuiKey_Tab;
  case gneiss::engine::physical_key_space:
    return ImGuiKey_Space;
  case gneiss::engine::physical_key_insert:
    return ImGuiKey_Insert;
  case gneiss::engine::physical_key_home:
    return ImGuiKey_Home;
  case gneiss::engine::physical_key_page_up:
    return ImGuiKey_PageUp;
  case gneiss::engine::physical_key_delete:
    return ImGuiKey_Delete;
  case gneiss::engine::physical_key_end:
    return ImGuiKey_End;
  case gneiss::engine::physical_key_page_down:
    return ImGuiKey_PageDown;
  case gneiss::engine::physical_key_right:
    return ImGuiKey_RightArrow;
  case gneiss::engine::physical_key_left:
    return ImGuiKey_LeftArrow;
  case gneiss::engine::physical_key_down:
    return ImGuiKey_DownArrow;
  case gneiss::engine::physical_key_up:
    return ImGuiKey_UpArrow;
  case gneiss::engine::physical_key_left_control:
    return ImGuiKey_LeftCtrl;
  case gneiss::engine::physical_key_left_shift:
    return ImGuiKey_LeftShift;
  case gneiss::engine::physical_key_left_alt:
    return ImGuiKey_LeftAlt;
  case gneiss::engine::physical_key_left_super:
    return ImGuiKey_LeftSuper;
  case gneiss::engine::physical_key_right_control:
    return ImGuiKey_RightCtrl;
  case gneiss::engine::physical_key_right_shift:
    return ImGuiKey_RightShift;
  case gneiss::engine::physical_key_right_alt:
    return ImGuiKey_RightAlt;
  case gneiss::engine::physical_key_right_super:
    return ImGuiKey_RightSuper;
  default:
    return ImGuiKey_None;
  }
}

void update_modifiers(ImGuiIO& io, std::uint32_t modifiers) noexcept {
  io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & (gneiss::engine::modifier_left_control_bit |
                                              gneiss::engine::modifier_right_control_bit)) != 0U);
  io.AddKeyEvent(ImGuiMod_Shift, (modifiers & (gneiss::engine::modifier_left_shift_bit |
                                               gneiss::engine::modifier_right_shift_bit)) != 0U);
  io.AddKeyEvent(ImGuiMod_Alt, (modifiers & (gneiss::engine::modifier_left_alt_bit |
                                             gneiss::engine::modifier_right_alt_bit)) != 0U);
  io.AddKeyEvent(ImGuiMod_Super, (modifiers & (gneiss::engine::modifier_left_super_bit |
                                               gneiss::engine::modifier_right_super_bit)) != 0U);
}

int map_mouse_button(std::uint32_t button) noexcept {
  switch (button) {
  case gneiss::engine::pointer_primary_bit:
    return ImGuiMouseButton_Left;
  case gneiss::engine::pointer_secondary_bit:
    return ImGuiMouseButton_Right;
  case gneiss::engine::pointer_middle_bit:
    return ImGuiMouseButton_Middle;
  case gneiss::engine::pointer_x1_bit:
    return 3;
  case gneiss::engine::pointer_x2_bit:
    return 4;
  default:
    return -1;
  }
}

gneiss::engine::result synchronize_input_state(runtime::application& application) noexcept {
  auto& io = ImGui::GetIO();
  const auto& keyboard = application.keyboard_state();
  update_modifiers(io, keyboard.modifiers);
  for (std::uint32_t physical_key = 0U; physical_key < 256U; ++physical_key) {
    const auto key = map_key(physical_key);
    if (key == ImGuiKey_None) {
      continue;
    }
    const auto mask = UINT64_C(1) << (physical_key % 64U);
    const auto is_down = (keyboard.pressed_keys[physical_key / 64U] & mask) != 0U;
    io.AddKeyEvent(key, is_down);
  }

  const auto& pointer = application.pointer_state();
  if (pointer.is_inside != 0U) {
    io.AddMousePosEvent(pointer.x, pointer.y);
  } else {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  }
  constexpr std::uint32_t pointer_buttons[] = {
      gneiss::engine::pointer_primary_bit, gneiss::engine::pointer_secondary_bit,
      gneiss::engine::pointer_middle_bit,  gneiss::engine::pointer_x1_bit,
      gneiss::engine::pointer_x2_bit,
  };
  for (const auto button : pointer_buttons) {
    io.AddMouseButtonEvent(map_mouse_button(button), (pointer.buttons & button) != 0U);
  }
  return gneiss::engine::result::success;
}

} // namespace

imgui_adapter::~imgui_adapter() noexcept {
  if (context_ != nullptr) {
    ImGui::DestroyContext(context_);
  }
}

gneiss::engine::result imgui_adapter::initialize(runtime::application& application) {
  if (!application.is_owner_thread() || !application.accepts_requests()) {
    return gneiss::engine::result::invalid_state;
  }
  if (context_ != nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  context_ = ImGui::CreateContext();
  if (context_ == nullptr) {
    return gneiss::engine::result::out_of_memory;
  }
  ImGui::SetCurrentContext(context_);
  apply_gneiss_mocha_theme();
  auto& io = ImGui::GetIO();
  io.BackendPlatformName = "gneiss_editor";
  io.BackendRendererName = "gneiss_ui_draw_list";
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  io.Fonts->Clear();
  ImFontConfig font_config{};
  font_config.SizePixels = 16.0F;
  font_config.PixelSnapH = false;
  font_config.OversampleH = 2;
  font_config.OversampleV = 2;
  font_config.RasterizerMultiply = 0.88F;
  io.FontDefault =
      io.Fonts->AddFontFromFileTTF(GNEISS_EDITOR_FONT_PATH, font_config.SizePixels, &font_config);
  if (io.FontDefault == nullptr) {
    io.FontDefault = io.Fonts->AddFontDefault(&font_config);
  }
  if (io.FontDefault == nullptr) {
    return gneiss::engine::result::out_of_memory;
  }

  ImFontConfig cjk_font_config{};
  cjk_font_config.MergeMode = true;
  cjk_font_config.PixelSnapH = false;
  cjk_font_config.OversampleH = 1;
  cjk_font_config.OversampleV = 1;
  cjk_font_config.GlyphOffset = ImVec2(0.0F, -1.0F);
  cjk_font_config.RasterizerMultiply = 1.25F;
  constexpr float cjk_font_size = 18.0F;
  if (io.Fonts->AddFontFromFileTTF(GNEISS_EDITOR_CJK_FONT_PATH, cjk_font_size, &cjk_font_config,
                                   io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) == nullptr) {
    return gneiss::engine::result::initialization_failed;
  }

  unsigned char* atlas_pixels = nullptr;
  int atlas_width = 0;
  int atlas_height = 0;
  io.Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_width, &atlas_height);
  if (atlas_pixels == nullptr || atlas_width <= 0 || atlas_height <= 0) {
    return gneiss::engine::result::invalid_state;
  }
  auto* baked_font = io.FontDefault->GetFontBaked(font_config.SizePixels);
  if (baked_font == nullptr ||
      baked_font->FindGlyphNoFallback(static_cast<ImWchar>(u'中')) == nullptr) {
    return gneiss::engine::result::initialization_failed;
  }
  const auto pixel_count =
      static_cast<std::size_t>(atlas_width) * static_cast<std::size_t>(atlas_height);
  std::vector<std::uint8_t> premultiplied(pixel_count * 4U);
  std::memcpy(premultiplied.data(), atlas_pixels, premultiplied.size());
  for (std::size_t index = 0; index < pixel_count; ++index) {
    const auto alpha = static_cast<std::uint32_t>(premultiplied[index * 4U + 3U]);
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      auto& value = premultiplied[index * 4U + channel];
      value = static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * alpha + 127U) / 255U);
    }
  }
  gneiss::engine::texture_view texture_desc;
  texture_desc.color_space = gneiss::engine::texture_color_space::linear;
  texture_desc.width = static_cast<std::uint32_t>(atlas_width);
  texture_desc.height = static_cast<std::uint32_t>(atlas_height);
  texture_desc.row_stride_bytes = texture_desc.width * 4U;
  texture_desc.pixels = premultiplied;
  const auto result =
      gneiss::engine::create_texture(application.resource_factory(), texture_desc, font_texture_);
  if (result.failed())
    return result;
  io.Fonts->SetTexID(static_cast<ImTextureID>(gneiss::engine::resource_handle(font_texture_)));
  io.Fonts->TexRef._TexData->SetStatus(ImTextureStatus_OK);
  return gneiss::engine::result::success;
}

void imgui_adapter::shutdown(runtime::application& application) noexcept {
  if (!application.is_owner_thread() || !application.accepts_requests()) {
    return;
  }
  ImGui::SetCurrentContext(context_);
  font_texture_.reset();
  if (context_ != nullptr) {
    ImGui::DestroyContext(context_);
    context_ = nullptr;
  }
}

gneiss::engine::result imgui_adapter::begin_frame(runtime::application& application,
                                                  const gneiss::engine::frame_time& time) {
  if (!application.is_owner_thread() || !application.accepts_requests()) {
    return gneiss::engine::result::invalid_state;
  }
  if (context_ == nullptr) {
    return gneiss::engine::result::invalid_state;
  }
  ImGui::SetCurrentContext(context_);
  gneiss::engine::input_event event;
  auto poll_result = application.poll_input(event);
  while (poll_result == gneiss::engine::result::success) {
    process_input(event);
    event = {};
    poll_result = application.poll_input(event);
  }
  if (poll_result != gneiss::engine::result::not_ready) {
    return poll_result;
  }
  const auto synchronize_result = synchronize_input_state(application);
  if (synchronize_result != gneiss::engine::result::success) {
    return synchronize_result;
  }
  auto& io = ImGui::GetIO();
  std::uint32_t window_width = 0U;
  std::uint32_t window_height = 0U;
  const auto window_result = application.get_window_size(window_width, window_height);
  if (window_result != gneiss::engine::result::success) {
    return window_result;
  }
  // 窗口创建、最小化或交换链调整期间，尺寸可能短暂为零；这不是致命错误。
  io.DisplaySize = ImVec2(static_cast<float>(std::max(window_width, UINT32_C(1))),
                          static_cast<float>(std::max(window_height, UINT32_C(1))));
  io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
  constexpr float default_delta_seconds = 1.0F / 60.0F;
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  io.DeltaTime =
      time.delta_ns == 0U
          ? default_delta_seconds
          : static_cast<float>(static_cast<double>(time.delta_ns) / nanoseconds_per_second);
  ImGui::NewFrame();
  return gneiss::engine::result::success;
}

void imgui_adapter::process_input(const gneiss::engine::input_event& event) {
  auto& io = ImGui::GetIO();
  switch (event.type) {
  case gneiss::engine::event_type::key: {
    update_modifiers(io, event.data.key.modifiers);
    const auto key = map_key(event.data.key.physical_key);
    if (key != ImGuiKey_None && event.data.key.action != gneiss::engine::key_action::repeated) {
      io.AddKeyEvent(key, event.data.key.action == gneiss::engine::key_action::pressed);
    }
    break;
  }
  case gneiss::engine::event_type::text: {
    const auto length = std::min<std::size_t>(event.data.text.length, sizeof(event.data.text.utf8));
    const std::string utf8{event.data.text.utf8, length};
    io.AddInputCharactersUTF8(utf8.c_str());
    break;
  }
  case gneiss::engine::event_type::pointer_moved:
    io.AddMousePosEvent(event.data.pointer_moved.x, event.data.pointer_moved.y);
    break;
  case gneiss::engine::event_type::pointer_button: {
    io.AddMousePosEvent(event.data.pointer_button.x, event.data.pointer_button.y);
    const auto button = map_mouse_button(event.data.pointer_button.button);
    if (button >= 0) {
      io.AddMouseButtonEvent(button, event.data.pointer_button.pressed != 0U);
    }
    break;
  }
  case gneiss::engine::event_type::pointer_wheel:
    io.AddMousePosEvent(event.data.pointer_wheel.x, event.data.pointer_wheel.y);
    io.AddMouseWheelEvent(event.data.pointer_wheel.delta_x, event.data.pointer_wheel.delta_y);
    break;
  case gneiss::engine::event_type::pointer_left:
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    break;
  default:
    break;
  }
}

gneiss::engine::result imgui_adapter::submit(runtime::application& application) {
  if (context_ == nullptr) {
    return gneiss::engine::result::invalid_state;
  }
  ImGui::SetCurrentContext(context_);
  ImGui::Render();
  const auto* draw_data = ImGui::GetDrawData();
  if (draw_data == nullptr) {
    return gneiss::engine::result::invalid_state;
  }
  vertices_.clear();
  indices_.clear();
  commands_.clear();
  vertices_.reserve(static_cast<std::size_t>(draw_data->TotalVtxCount));
  indices_.reserve(static_cast<std::size_t>(draw_data->TotalIdxCount));

  std::uint32_t vertex_base = 0;
  std::uint32_t index_base = 0;
  for (const auto* source : draw_data->CmdLists) {
    for (const auto& vertex : source->VtxBuffer) {
      vertices_.push_back(
          {{vertex.pos.x - draw_data->DisplayPos.x, vertex.pos.y - draw_data->DisplayPos.y},
           {vertex.uv.x, vertex.uv.y},
           // Granit Canvas 使用预乘 Alpha，顶点色必须与字体图集采用相同约定。
           premultiply_color(vertex.col)});
    }
    for (const auto index : source->IdxBuffer) {
      indices_.push_back(static_cast<std::uint32_t>(index));
    }
    for (const auto& command : source->CmdBuffer) {
      if (command.UserCallback != nullptr || command.ElemCount == 0U ||
          command.ClipRect.x >= command.ClipRect.z || command.ClipRect.y >= command.ClipRect.w) {
        continue;
      }
      const auto texture = static_cast<gneiss::engine::rid>(command.GetTexID());
      gneiss::engine::ui_draw_command converted{};
      converted.texture = application.resource_factory().find_texture(texture);
      converted.clip_min[0] = command.ClipRect.x - draw_data->DisplayPos.x;
      converted.clip_min[1] = command.ClipRect.y - draw_data->DisplayPos.y;
      converted.clip_max[0] = command.ClipRect.z - draw_data->DisplayPos.x;
      converted.clip_max[1] = command.ClipRect.w - draw_data->DisplayPos.y;
      converted.first_index = index_base + command.IdxOffset;
      converted.index_count = command.ElemCount;
      converted.vertex_offset = vertex_base + command.VtxOffset;
      commands_.push_back(converted);
    }
    vertex_base += static_cast<std::uint32_t>(source->VtxBuffer.Size);
    index_base += static_cast<std::uint32_t>(source->IdxBuffer.Size);
  }

  gneiss::engine::ui_draw_view desc;
  desc.display_width = draw_data->DisplaySize.x;
  desc.display_height = draw_data->DisplaySize.y;
  desc.framebuffer_scale_x = draw_data->FramebufferScale.x;
  desc.framebuffer_scale_y = draw_data->FramebufferScale.y;
  desc.vertices = vertices_;
  desc.indices = indices_;
  desc.commands = commands_;
  if (!application.is_owner_thread())
    return gneiss::engine::result::invalid_state;
  return application.submit_ui_draw_list(desc);
}

} // namespace gneiss::editor
