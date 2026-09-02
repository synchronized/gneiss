// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "imgui_adapter.h"

#include "editor_theme.h"

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
  if (key >= GNEISS_PHYSICAL_KEY_A && key <= GNEISS_PHYSICAL_KEY_Z) {
    return static_cast<ImGuiKey>(ImGuiKey_A + key - GNEISS_PHYSICAL_KEY_A);
  }
  if (key >= GNEISS_PHYSICAL_KEY_1 && key <= GNEISS_PHYSICAL_KEY_9) {
    return static_cast<ImGuiKey>(ImGuiKey_1 + key - GNEISS_PHYSICAL_KEY_1);
  }
  if (key >= GNEISS_PHYSICAL_KEY_F1 && key <= GNEISS_PHYSICAL_KEY_F12) {
    return static_cast<ImGuiKey>(ImGuiKey_F1 + key - GNEISS_PHYSICAL_KEY_F1);
  }
  switch (key) {
  case GNEISS_PHYSICAL_KEY_0:
    return ImGuiKey_0;
  case GNEISS_PHYSICAL_KEY_ENTER:
    return ImGuiKey_Enter;
  case GNEISS_PHYSICAL_KEY_ESCAPE:
    return ImGuiKey_Escape;
  case GNEISS_PHYSICAL_KEY_BACKSPACE:
    return ImGuiKey_Backspace;
  case GNEISS_PHYSICAL_KEY_TAB:
    return ImGuiKey_Tab;
  case GNEISS_PHYSICAL_KEY_SPACE:
    return ImGuiKey_Space;
  case GNEISS_PHYSICAL_KEY_INSERT:
    return ImGuiKey_Insert;
  case GNEISS_PHYSICAL_KEY_HOME:
    return ImGuiKey_Home;
  case GNEISS_PHYSICAL_KEY_PAGE_UP:
    return ImGuiKey_PageUp;
  case GNEISS_PHYSICAL_KEY_DELETE:
    return ImGuiKey_Delete;
  case GNEISS_PHYSICAL_KEY_END:
    return ImGuiKey_End;
  case GNEISS_PHYSICAL_KEY_PAGE_DOWN:
    return ImGuiKey_PageDown;
  case GNEISS_PHYSICAL_KEY_RIGHT:
    return ImGuiKey_RightArrow;
  case GNEISS_PHYSICAL_KEY_LEFT:
    return ImGuiKey_LeftArrow;
  case GNEISS_PHYSICAL_KEY_DOWN:
    return ImGuiKey_DownArrow;
  case GNEISS_PHYSICAL_KEY_UP:
    return ImGuiKey_UpArrow;
  case GNEISS_PHYSICAL_KEY_LEFT_CONTROL:
    return ImGuiKey_LeftCtrl;
  case GNEISS_PHYSICAL_KEY_LEFT_SHIFT:
    return ImGuiKey_LeftShift;
  case GNEISS_PHYSICAL_KEY_LEFT_ALT:
    return ImGuiKey_LeftAlt;
  case GNEISS_PHYSICAL_KEY_LEFT_SUPER:
    return ImGuiKey_LeftSuper;
  case GNEISS_PHYSICAL_KEY_RIGHT_CONTROL:
    return ImGuiKey_RightCtrl;
  case GNEISS_PHYSICAL_KEY_RIGHT_SHIFT:
    return ImGuiKey_RightShift;
  case GNEISS_PHYSICAL_KEY_RIGHT_ALT:
    return ImGuiKey_RightAlt;
  case GNEISS_PHYSICAL_KEY_RIGHT_SUPER:
    return ImGuiKey_RightSuper;
  default:
    return ImGuiKey_None;
  }
}

void update_modifiers(ImGuiIO& io, std::uint32_t modifiers) noexcept {
  io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & (GNEISS_MODIFIER_LEFT_CONTROL_BIT |
                                              GNEISS_MODIFIER_RIGHT_CONTROL_BIT)) != 0U);
  io.AddKeyEvent(ImGuiMod_Shift, (modifiers & (GNEISS_MODIFIER_LEFT_SHIFT_BIT |
                                               GNEISS_MODIFIER_RIGHT_SHIFT_BIT)) != 0U);
  io.AddKeyEvent(ImGuiMod_Alt, (modifiers & (GNEISS_MODIFIER_LEFT_ALT_BIT |
                                             GNEISS_MODIFIER_RIGHT_ALT_BIT)) != 0U);
  io.AddKeyEvent(ImGuiMod_Super, (modifiers & (GNEISS_MODIFIER_LEFT_SUPER_BIT |
                                               GNEISS_MODIFIER_RIGHT_SUPER_BIT)) != 0U);
}

int map_mouse_button(std::uint32_t button) noexcept {
  switch (button) {
  case GNEISS_POINTER_PRIMARY_BIT:
    return ImGuiMouseButton_Left;
  case GNEISS_POINTER_SECONDARY_BIT:
    return ImGuiMouseButton_Right;
  case GNEISS_POINTER_MIDDLE_BIT:
    return ImGuiMouseButton_Middle;
  case GNEISS_POINTER_X1_BIT:
    return 3;
  case GNEISS_POINTER_X2_BIT:
    return 4;
  default:
    return -1;
  }
}

gneiss_result synchronize_input_state(gneiss_application application) noexcept {
  auto& io = ImGui::GetIO();
  gneiss_keyboard_state keyboard = GNEISS_KEYBOARD_STATE_INIT;
  auto result = gneiss_application_get_keyboard_state(application, &keyboard);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
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

  gneiss_pointer_state pointer = GNEISS_POINTER_STATE_INIT;
  result = gneiss_application_get_pointer_state(application, &pointer);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  if (pointer.is_inside != 0U) {
    io.AddMousePosEvent(pointer.x, pointer.y);
  } else {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  }
  constexpr std::uint32_t pointer_buttons[] = {
      GNEISS_POINTER_PRIMARY_BIT, GNEISS_POINTER_SECONDARY_BIT, GNEISS_POINTER_MIDDLE_BIT,
      GNEISS_POINTER_X1_BIT,      GNEISS_POINTER_X2_BIT,
  };
  for (const auto button : pointer_buttons) {
    io.AddMouseButtonEvent(map_mouse_button(button), (pointer.buttons & button) != 0U);
  }
  return GNEISS_SUCCESS;
}

} // namespace

imgui_adapter::~imgui_adapter() noexcept {
  if (context_ != nullptr) {
    ImGui::DestroyContext(context_);
  }
}

gneiss_result imgui_adapter::initialize(gneiss_application application) {
  if (application == GNEISS_NULL_APPLICATION || context_ != nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  context_ = ImGui::CreateContext();
  if (context_ == nullptr) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
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
    return GNEISS_ERROR_OUT_OF_MEMORY;
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
    return GNEISS_ERROR_INITIALIZATION_FAILED;
  }

  unsigned char* atlas_pixels = nullptr;
  int atlas_width = 0;
  int atlas_height = 0;
  io.Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_width, &atlas_height);
  if (atlas_pixels == nullptr || atlas_width <= 0 || atlas_height <= 0) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  auto* baked_font = io.FontDefault->GetFontBaked(font_config.SizePixels);
  if (baked_font == nullptr ||
      baked_font->FindGlyphNoFallback(static_cast<ImWchar>(u'中')) == nullptr) {
    return GNEISS_ERROR_INITIALIZATION_FAILED;
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
  gneiss_texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  texture_desc.color_space = GNEISS_TEXTURE_COLOR_SPACE_LINEAR;
  texture_desc.width = static_cast<std::uint32_t>(atlas_width);
  texture_desc.height = static_cast<std::uint32_t>(atlas_height);
  texture_desc.row_stride_bytes = texture_desc.width * 4U;
  texture_desc.pixel_data_size = static_cast<std::uint64_t>(premultiplied.size());
  texture_desc.pixels = premultiplied.data();
  const auto result = gneiss_texture_create(application, &texture_desc, &font_texture_);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  io.Fonts->SetTexID(static_cast<ImTextureID>(font_texture_));
  io.Fonts->TexRef._TexData->SetStatus(ImTextureStatus_OK);
  return GNEISS_SUCCESS;
}

void imgui_adapter::shutdown(gneiss_application application) noexcept {
  ImGui::SetCurrentContext(context_);
  if (font_texture_ != GNEISS_NULL_TEXTURE && application != GNEISS_NULL_APPLICATION) {
    (void)gneiss_texture_destroy(application, font_texture_);
    font_texture_ = GNEISS_NULL_TEXTURE;
  }
  if (context_ != nullptr) {
    ImGui::DestroyContext(context_);
    context_ = nullptr;
  }
}

gneiss_result imgui_adapter::begin_frame(gneiss_application application,
                                         const gneiss_frame_time& time) {
  if (context_ == nullptr) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  ImGui::SetCurrentContext(context_);
  gneiss_input_event event = GNEISS_INPUT_EVENT_INIT;
  auto poll_result = gneiss_application_poll_input(application, &event);
  while (poll_result == GNEISS_SUCCESS) {
    process_input(event);
    event = GNEISS_INPUT_EVENT_INIT;
    poll_result = gneiss_application_poll_input(application, &event);
  }
  if (poll_result != GNEISS_ERROR_NOT_READY) {
    return poll_result;
  }
  const auto synchronize_result = synchronize_input_state(application);
  if (synchronize_result != GNEISS_SUCCESS) {
    return synchronize_result;
  }
  auto& io = ImGui::GetIO();
  std::uint32_t window_width = 0U;
  std::uint32_t window_height = 0U;
  const auto window_result =
      gneiss_application_get_window_size(application, &window_width, &window_height);
  if (window_result != GNEISS_SUCCESS) {
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
  return GNEISS_SUCCESS;
}

void imgui_adapter::process_input(const gneiss_input_event& event) {
  auto& io = ImGui::GetIO();
  switch (event.type) {
  case GNEISS_INPUT_EVENT_KEY: {
    update_modifiers(io, event.data.key.modifiers);
    const auto key = map_key(event.data.key.physical_key);
    if (key != ImGuiKey_None && event.data.key.action != GNEISS_KEY_REPEATED) {
      io.AddKeyEvent(key, event.data.key.action == GNEISS_KEY_PRESSED);
    }
    break;
  }
  case GNEISS_INPUT_EVENT_TEXT: {
    const auto length = std::min<std::size_t>(event.data.text.length, GNEISS_INPUT_TEXT_CAPACITY);
    const std::string utf8{event.data.text.utf8, length};
    io.AddInputCharactersUTF8(utf8.c_str());
    break;
  }
  case GNEISS_INPUT_EVENT_POINTER_MOVED:
    io.AddMousePosEvent(event.data.pointer_moved.x, event.data.pointer_moved.y);
    break;
  case GNEISS_INPUT_EVENT_POINTER_BUTTON: {
    io.AddMousePosEvent(event.data.pointer_button.x, event.data.pointer_button.y);
    const auto button = map_mouse_button(event.data.pointer_button.button);
    if (button >= 0) {
      io.AddMouseButtonEvent(button, event.data.pointer_button.pressed != 0U);
    }
    break;
  }
  case GNEISS_INPUT_EVENT_POINTER_WHEEL:
    io.AddMousePosEvent(event.data.pointer_wheel.x, event.data.pointer_wheel.y);
    io.AddMouseWheelEvent(event.data.pointer_wheel.delta_x, event.data.pointer_wheel.delta_y);
    break;
  case GNEISS_INPUT_EVENT_POINTER_LEFT:
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    break;
  default:
    break;
  }
}

gneiss_result imgui_adapter::submit(gneiss_application application) {
  if (context_ == nullptr) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  ImGui::SetCurrentContext(context_);
  ImGui::Render();
  const auto* draw_data = ImGui::GetDrawData();
  if (draw_data == nullptr) {
    return GNEISS_ERROR_INVALID_STATE;
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
      const auto texture = static_cast<gneiss_texture>(command.GetTexID());
      gneiss_ui_draw_command converted{};
      converted.texture = texture;
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

  gneiss_ui_draw_list_desc desc = GNEISS_UI_DRAW_LIST_DESC_INIT;
  desc.display_width = draw_data->DisplaySize.x;
  desc.display_height = draw_data->DisplaySize.y;
  desc.framebuffer_scale_x = draw_data->FramebufferScale.x;
  desc.framebuffer_scale_y = draw_data->FramebufferScale.y;
  desc.vertex_count = static_cast<std::uint32_t>(vertices_.size());
  desc.vertices = vertices_.data();
  desc.index_count = static_cast<std::uint32_t>(indices_.size());
  desc.indices = indices_.data();
  desc.command_count = static_cast<std::uint32_t>(commands_.size());
  desc.commands = commands_.data();
  return gneiss_application_submit_ui_draw_list(application, &desc);
}

} // namespace gneiss::editor
