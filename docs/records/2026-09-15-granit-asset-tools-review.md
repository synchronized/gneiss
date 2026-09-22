<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Granit 0.23.0 AssetTools 接入评审

## 范围与结论

2026-09-15 检查远端 Release、main 和本地下载的正式发布源码。最新正式版本为 v0.23.0，提交为
`496a298ce9e94cb616790df1ddd95e051cea7cb8`；main 后续只有发布验收文档提交。统一 AssetTools
由 [PR #65](https://github.com/synchronized/granit/pull/65) 引入，空帧修复由
[PR #67](https://github.com/synchronized/granit/pull/67) 引入。

整体职责合理：AssetTools 是独立可选组件，提供 Shader、Material、Texture、Environment 的公共
C/C++ 构建接口；Runtime 不传递链接 AssetTools。Texture 接收已经编码的 GPU 负载，Environment
接收预处理像素，不接管 Gneiss 图片导入、BC7 编码、工程缓存、资产 URI 或 RID。

Gneiss 应复用其 Manifest、摘要和内容 ID 生成，避免保留第二套 Granit 格式编码实现；运行纹理
外层封装、变体顺序和项目缓存仍由 Gneiss 管理。

## 建议 PR 1：AssetTools 结果句柄跨类型校验

优先级：P1，已用 v0.23.0 Windows Shared DLL 复现。

`tools/asset_tools_material_api.cpp`、`asset_tools_texture_api.cpp` 和
`asset_tools_environment_api.cpp` 各自使用从 1 开始的计数器和独立 map。C ABI 的三种结果都是
`uint64_t`，相同数值可能同时代表三个活跃对象；查找和销毁未验证句柄的领域类型。

复现步骤：分别调用三个领域的 inspect，输入单字节 `x`。三个接口返回 `invalid_argument`，
并按诊断结果契约各自产生非零结果句柄。实际输出为：

```text
material: status=-2, handle=1
texture: status=-2, handle=1
environment: status=-2, handle=1
material_result_destroy(texture_handle): 0
material_result_destroy(material_handle): -3
```

错误类型的销毁成功，真正的 Material 结果已被误销毁；错误类型查询同样存在别名风险。

最小修复：采用统一分配且记录类型的结果注册表，或在句柄中编码领域类型并校验。若复用槽位，
同时校验 generation。扩展检查 Shader Compiler、Compilation、Reflection 等 AssetTools 句柄。
无需改变公开整数句柄类型。

验收：各类活跃句柄两两混用查询和销毁均返回 `invalid_handle`，原对象仍有效；覆盖失败诊断结果、
零值、重复销毁、旧句柄失效和正常并发访问。此项是通用 SDK 正确性问题，不需要 Gneiss 专用语义。

## 建议 PR 2：分离 AssetTools SDK 与 CLI 构建/安装

优先级：P2，源码和 Gneiss 安装测试均已确认。

配置 `GRANIT_BUILD_ASSET_TOOLS=ON`、`GRANIT_BUILD_TOOLS=OFF` 时，`tools/CMakeLists.txt` 仍因
`TARGET granit_asset_tools` 存在而创建 `granit_asset_tool`，并将 CLI 放入同一 AssetTools 安装组件。
Gneiss 通过 FetchContent `EXCLUDE_FROM_ALL` 接入时，仅链接 SDK 不会构建 CLI，但完整安装 Granit
SDK 会因缺少 `granit_asset_tool.exe` 失败。

最小修复：SDK 和 CLI 分别受明确选项控制，并拆分安装组件。若内建资产重新生成需要 CLI，应显式
表达该工具依赖；只使用已发布资产快照和 SDK 的下游不应被要求构建 CLI。

验收：SDK-only 构建可链接并安装，CLI 关闭时不安装缺失的可执行文件；CLI 开启时功能与安装完整；
增加 FetchContent EXCLUDE_FROM_ALL Consumer，并覆盖 Shared/Static。Gneiss 当前通过测试构建目标
补齐 CLI，保证既有完整安装验证可执行；上游修复后可删除这项接入补偿。

## 暂不建议扩大的范围

- 不把 Gneiss 项目构建图、缓存目录、PNG/KTX2 作者语义、RID 或包清单下沉到 Granit。
- 不因统一 SDK 而让 Runtime 链接 AssetTools。
- Shader 编译取消、结构化源位置诊断、编译器身份查询等应由实际接入需求驱动，本次没有足够证据
  将其列为阻塞问题。

本次未修改 Granit 仓库、提交上游 PR 或触发其工作流。
