<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# 编辑材质参数

在 Asset Browser 中选择作者材质（`.material.json`），点击 **Edit Material**。
导入器生成的输出不提供此入口，避免重新导入覆盖手工修改。

Material Editor 按反射字段展开 parameters、sampling 和 texture_uris。
颜色、浮点数、布尔值及整数可直接编辑；纹理 URI 输入后按 Enter 提交，空串表示不引用纹理。
固定数组允许修改元素，不提供增删。当前枚举使用整数控件，值域由材质校验约束；
例如 alpha_mode 为 0（opaque）、1（mask）、2（blend）。非法修改报告错误并保留原值。

- **Undo Material / Redo Material**：材质独立历史，不影响场景历史。连续拖动同一字段合并为一次编辑。
- **Save Material**：校验并异步保存；成功后星号消失。保存期间的新修改继续保留星号。
- **Discard Changes**：确认后恢复最后成功保存的描述，并清空该材质历史。

面板聚焦时 Ctrl+S 保存材质，Ctrl+Z 撤销，Ctrl+Shift+Z 重做；文本输入中的编辑由文本控件处理。
关闭面板、退出编辑器或切换材质前，需要先保存或丢弃未保存内容。保存进行中不能丢弃或切换。

保存前会比较文件与加载/上次保存基线，外部修改导致冲突时不会覆盖文件。
可先保留所需修改内容，再丢弃并从 Asset Browser 重新打开最新文件。
当前编辑只作用于描述副本，不直接修改资产缓存，也不提供实时 GPU 预览。
文件落盘后的资源更新沿用宿主已有的文件监听流程。
