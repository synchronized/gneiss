<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 同步输入准备规则收敛

## 变更

Application 同步输入加载交给 asset_manager，取消旧异步采用后进行准备，成功才替换 InputService。
同步与 CAORS 异步路径共用 1 MiB 文档上限和来源复验函数；复验比较完整字节，
既检测长度改变，也检测长度相同的内容改变。同步输出仅在解析和复验成功后更新。

同步调用不要求绑定执行器，不等待或推进外部任务队列；I/O 仍在调用线程运行。
公共函数签名与 ABI 未改变，新增大小上限和复验错误说明。

## 验证

- Windows Clang Debug：`gneiss.action_map`、`gneiss.caors.application`、
  `gneiss.c_application`、`gneiss.cpp_input` 共 4 项通过。
- 新增可控 VFS 测试：禁止无界读取、成功复验、同长度内容变化、复读错误、超限；
  失败均保留原输出。
- Application 新增未绑定执行器时同步成功、同步与异步超限结果一致验证。
- 原生无线程静态 Engine：Application 测试通过。
- Emscripten 静态 Engine，Node/MEMFS：Application 测试通过。

## 剩余范围

本次仅统一准备阶段。同步结果尚未发布到 CAORS 缓存；缓存、GUID、发布代次及在途旧请求
协调仍需迁移，不宣称同步与异步实现已全部统一。浏览器网络 I/O、远端 Actions 未验证。
