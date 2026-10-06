<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Asset CPU 准备闭环

日期：2026-10-05。按 ADR-054 完成 CPU 准备与渲染发布的依赖分离。

## 实施结果

CPU 准备和共用解析迁至 `src/engine/asset/asset_preparation.hpp/.cpp` 与 `asset_parsing.hpp`，
类型和函数进入 `asset_internal`。准备实现仍是唯一实现，不持有缓存、RID 表、资源服务或上传回执。
独立准备测试归入 `tests/asset/asset_preparation_test.cpp`，保留既有 target 和 CTest 名称。

Render 的 `render_asset_preparation.hpp` 只适配资源类型名，并调用 Asset 准备函数，显式传入
`sizeof(material_resource)`。因此原有批次上限、单材质发布字节估算和 URI 字节计数保持不变；
同步解析、纹理变体选择、源版本复验、取消和发布事务仍沿用现有路径。

Asset 不知道发布资源布局。含材质时，调用方必须给出非零 `material_bytes`；预算超过批次上限
或加入 URI 后溢出/超限时拒绝整批并清空输出。原始解析错误仍优先返回。
增加预算恰好用满、零估算、估算超限和 `SIZE_MAX` 加 URI 的拒绝测试。

边界检查覆盖准备实现对 Render、缓存和 RID 表的非法包含。现有网格、材质、纹理数据与
发布适配只有一份，不建立平行资源状态，也不改变公共 C/C++ SDK。

## 剩余工作

本项完成的是 Asset 准备与 Render 发布边界；Function 模块布局、Editor 与 Apps 的分离以及
完整 C++ SDK 审计仍按 DEV-045 推进。未执行本版发布操作，不能据此宣称 0.45 已完成。

## 验证结果

- Windows Clang Debug 共享全量构建成功，完整 CTest 175/175 通过（185.08 秒）。
- 静态相关目标构建成功，准备、加载、场景、像素及边界相关 CTest 11/11 通过（19.41 秒）。
- 独立准备测试的链接输入不含 Engine、缓存或资源发布实现。
- 准备测试 `clang-tidy` 复查无自有代码告警；348 个修改文档中的相对链接校验通过。
- 提交前 `git diff --check` 通过。未运行远端 Actions、跨平台完整矩阵或外部大型场景。
