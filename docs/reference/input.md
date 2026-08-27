<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 输入接口

## 当前能力

`gneiss/input.h` 提供不依赖平台后端的 C11 输入 ABI，`gneiss/input.hpp` 提供轻量 C++20 包装。
Application 使用 Granit 平台时，会在每次业务更新回调前完成窗口事件采集，并形成当前帧键盘和
指针快照。核心无窗口模式下快照保持为空，事件查询返回 `GNEISS_ERROR_NOT_READY`。

原始事件通过 `gneiss_application_poll_input` 顺序读取，包含逻辑窗口 ID、单调时钟时间戳以及键盘、
UTF-8 文本或指针负载。物理键值采用 USB HID Keyboard/Keypad usage；指针坐标是窗口内容区逻辑
坐标。文本负载最多保存 48 字节，且由后端保证不截断 UTF-8 码点。

## 帧与生命周期

- 输入只能在 Application 创建线程查询。
- 当前帧事件在下一次平台轮询开始时清空；业务应在当帧 `update` 回调内消费。
- 键盘和指针查询返回值副本，不借用后端内存。
- 焦点丢失会清空按键、指针按钮和窗口内状态，避免保持状态卡住。
- 首版队列固定容纳 256 条事件；溢出时清空保持状态并令 Application 本帧返回
  `GNEISS_ERROR_INVALID_STATE`，不会静默丢弃释放事件。
- Application 销毁后，使用旧句柄查询返回 `GNEISS_ERROR_INVALID_HANDLE`。

Granit 的类型、句柄和枚举只存在于 `src/platform/granit/`，不属于 Gneiss 公共 ABI。版本化动作
映射在原始快照之上提供 `pressed`、`held`、`released` 和标量值，格式与句柄规则见
[输入动作映射格式 v1](input-map-format.md)。
