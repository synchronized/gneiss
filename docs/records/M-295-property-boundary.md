<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：属性编辑模型脱离 IPC

日期：2026-10-06。

## 实施范围

将 `runtime_property_edits` 迁至 `src/editor/` 的独立编辑模型目标。使用 Editor 自有的
带 generation 对象身份、类型化属性值、写入请求和结果，不再包含宿主协议头。
模型继续唯一持有会话、命令序号、pending 和属性响应状态，不建立平行状态机。

Apps 的 `runtime_property_adapter.hpp` 在接收和发送边界移动负载、消息并映射关联 ID、
对象身份、字段、修订号及结果码。Runtime 宿主显式包含协议头；主程序和灯廊工作流
显式提取对象 ID 与 generation，避免依赖协议类型的隐式传播。

排除类型名、include 和空白后，状态机实现与迁移前一致。独立模型测试保留乱序、重复、
过期会话、单字段 pending、拒绝、超时、迟到和断连验证。宿主测试新增全部十种属性负载
及元数据双向映射、临时协议响应销毁后的值与长消息所有权检查。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor、工程/IPC 与边界 CTest 49/49 通过，47.16 秒。
- 模型实际编译命令不含仓库 Apps 路径，独立模型测试链接不含宿主、工程或 IPC 协议库。
- 状态机内容比对、模型/适配/测试 clang-format 检查通过；修改文档相对链接与 git diff --check 通过。
- 本轮未重跑静态、完整 Engine 单测或跨平台 Actions；未验证分配失败故障注入。

## 剩余工作

场景镜像仍依赖检查协议；工程/进程装配与面板编排仍待拆分。
未改变公共 ABI 或线协议，不将本轮结果视为整版完成。
