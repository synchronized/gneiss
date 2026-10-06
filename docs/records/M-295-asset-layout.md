<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Asset 基础能力与上传服务归属

日期：2026-10-05。依照 ADR-054 分离资产基础能力与渲染上传事务。

## 实施范围

VFS、只读源、源版本复验、缓存、URI、网格与纹理格式、PNG 解码迁至 `src/engine/asset/`，
这组内部头统一使用 `.hpp`。工具、加载器、宿主、测试和 CMake 同步改用实际路径。

原 `texture_load_service` 持有 Render 缓存租约、上传回执和发布候选，属于渲染事务服务。
实现及头迁至 `src/render/`，命名空间改为 `render_internal`，专项测试移至 `tests/render/`。
Application 仍负责实例装配；服务仍在所属线程协调后台 CPU 准备、GPU 上传、提交与回滚。
不复制实现、不新增资源所有者、不修改取消、预算、迟到结果或停止语义。

Asset 对上层的直接包含检查扩大到整个目录，覆盖旧路径与目标 Function 路径的反例。
迁出上传事务后，Asset 不再直接包含 Render；此文本检查不替代完整依赖图。

## 仍待完成

`render_asset_loader` 中的解析与 CPU 准备仍使用 Render 数据契约，和缓存租约、RID 发布在同一
实现文件内。后续须拆开准备数据与资源发布接口，不能将本次目录迁移描述为全部加载边界完成。
本轮没有改公共 C/C++ SDK 或升级 Granit。

## 验证结果

- Windows Clang Debug 共享库、静态库配置均完成全量构建。
- 共享库完整 CTest：174/174 通过，185.03 秒。
- 静态库资产、纹理加载、场景加载、边界与安装后 consumer 相关 CTest：21/21 通过，30.59 秒。
- Asset 边界正反例检查通过；修改文档中的 340 个相对链接校验通过。
- 提交前 `git diff --check` 通过。

本轮未运行远端 Actions、Linux/macOS 或外部大型场景验证；上述结果不构成 0.45 完整发布验收。
