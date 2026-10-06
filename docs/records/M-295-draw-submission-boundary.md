<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：UI 与 Debug 绘制提交边界

日期：2026-10-06。

## 变更

Application 与 Render 的 UI 提交改为 `ui_draw_view`，Debug 提交改为线段 span。
API 适配器负责公开描述尺寸、保留字段和数组借用范围；内部继续校验数量上限、数值、裁剪、索引与纹理 RID。
成功提交复制数组，失败保留上一份列表，帧快照与资源持有不变。

应用所有者线程和 update 阶段检查保持在描述转换之前；非法描述在非 update 阶段仍返回 invalid_state。
API 查询阶段资格后，内部提交仍有阶段防护。没有新增绘制缓存、公共接口或 ABI 布局。

## 验证

- 共享完整构建及编辑器、C Application、绘制列表、帧包、Granit 冒烟、GPU 像素和边界回归 47/47 通过，60.80 秒。
- 循环改为范围遍历后，静态专项构建与 C Application、UI/Debug、帧包、边界正反例 6/6 通过，2.70 秒。
  同组共享测试重新构建后 6/6 通过，2.80 秒。
- 真实 C update 回调增加非法尺寸、保留字段及空数组指针拒绝验证，并固定非 update 阶段的错误优先级。
- 原有 UI 数据复制与失败保留测试继续通过；Debug 增加源数据变更后旧值不变及空提交清空测试。
- clang-tidy 检查三个修改实现文件，没有自有代码告警。

这完成了 Render 创建与绘制提交的版本化描述分离；Scene 与剩余编辑器职责、最终平台矩阵和发布仍未完成。
