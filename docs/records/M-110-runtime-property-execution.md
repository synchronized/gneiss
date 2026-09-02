<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-110：Runtime 属性写入执行实施记录

## 结论

M-110 已完成。Runtime 现可协商 `runtime_property_edit_v1`，通过 IPC 接收属性命令，并只在 Runtime
主线程安全点访问 Scene Tree、ECS 和反射属性系统。Editor 侧编辑交互与待处理状态留给 M-111、M-112。

## 已实现内容

- IPC 会话将已解码的拥有型属性命令交给主线程，每次 `pump` 最多接收 32 条；超过预算的命令收到
  `not ready` 结果，不形成无界积压。
- Runtime 检查镜像维护运行时对象 ID、generation 到原生实体的会话内映射；完整重采样后原子替换，
  旧对象和旧 generation 无法命中新实体。
- 主线程执行器复用 World 反射注册表与既有 getter/setter，校验会话、对象、字段可写能力、值类型和
  期望修订号，不直接访问组件成员偏移。
- 每个对象字段的修订号从 1 开始，只在写入成功且规范值读取成功后递增；失败不改变修订号。
- 成功响应返回 Runtime 重新读取的规范值；会话失效、对象失效、字段不存在或不可写、类型错误与
  修订冲突作为命令结果返回，不使 IPC 会话或 Runtime 主循环退出。
- 属性命令在 Runtime 运行和暂停状态下均可执行；I/O 线程仍只负责 socket 与帧传输。

## 验证结果

- 属性执行器测试覆盖 Transform 平移写入、规范值、修订递增、过期修订、对象失效和会话失效。
- IPC 会话测试覆盖能力协商、命令交付主线程以及结果帧往返。
- Windows Clang Shared Debug 全量构建通过，CTest 105/105 通过。
- 公共 C ABI 未变化；实现只扩展私有 Runtime 与 IPC 层。

## 后续边界

M-111 将在 Editor 建立命令发送、单属性在途状态、结果关联与会话清理。M-112 再开放 Runtime
Inspector 的 Transform 编辑交互；本阶段没有改变作者场景、撤销历史或保存行为。
