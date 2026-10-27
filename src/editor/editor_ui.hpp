// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_UI_H_
#define GNEISS_APPS_EDITOR_EDITOR_UI_H_

#include "engine/core/result.hpp"

#include "engine/platform/files/file_service.hpp"

namespace gneiss::editor {

enum class toolbar_icon { run, pause, stop };

struct editor_panel_visibility final {
  bool scene_hierarchy = true;
  bool asset_browser = true;
  bool scene_view = true;
  bool inspector = true;
  bool console = true;
};

/** 布局异步状态；回调仅持有回执，advance 必须在 ImGui 帧开始前调用。
 * 服务由宿主驱动并保活至保存完成；销毁对象不阻塞，也不访问 ImGui。 */
class editor_layout final {
public:
  editor_layout() = default;
  editor_layout(const editor_layout&) = delete;
  editor_layout& operator=(const editor_layout&) = delete;
  [[nodiscard]] engine::result initialize(std::shared_ptr<engine::file_service> files,
                                          std::string_view project_key,
                                          editor_panel_visibility& visibility) noexcept;
  [[nodiscard]] engine::result advance(editor_panel_visibility& visibility) noexcept;
  [[nodiscard]] engine::result save(const editor_panel_visibility& visibility) noexcept;
  [[nodiscard]] bool loading() const noexcept { return read_ != nullptr; }
  [[nodiscard]] bool saving() const noexcept { return write_ && !write_->complete; }
  [[nodiscard]] engine::result save_result() const noexcept;
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }

private:
  struct read_receipt {
    bool complete{};
    engine::file_completion<engine::file_bytes> value{engine::result::not_ready};
  };
  struct write_receipt {
    bool complete{};
    engine::result status{engine::result::not_ready};
  };
  std::shared_ptr<engine::file_service> files_;
  std::shared_ptr<read_receipt> read_;
  std::shared_ptr<write_receipt> write_;
  std::string uri_;
  std::uint64_t reset_generation_{};
};

/** 丢弃当前布局，并在本帧重建默认工作区。 */
void reset_editor_layout() noexcept;

/** 创建当前帧的单窗口 Editor DockSpace。 */
void begin_editor_workspace() noexcept;

/** 绘制不持有业务状态的工具栏图标按钮。 */
[[nodiscard]] bool toolbar_icon_button(const char* id, toolbar_icon icon, const char* tooltip,
                                       bool enabled, bool active = false) noexcept;

} // namespace gneiss::editor

#endif
