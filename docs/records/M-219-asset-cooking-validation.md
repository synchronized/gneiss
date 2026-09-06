<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-213～M-219：0.33.0 资产构建与发布优化验收记录

## 结果

Gneiss 0.33.0 已在发布工作流中接入确定性资产构建图、内容寻址缓存、格式校验和 Shipping 可达性
裁剪。工程格式 v5 通过 `asset_build.retain` 声明动态可达根；Development 包含全部运行时资产和
不泄漏绝对路径的构建诊断元数据，Shipping 只包含入口及其传递依赖。

首批处理器验证 PNG、JPEG、KTX2、静态 Mesh、Material 与 JSON 载荷，并保留当前 Runtime 可直接
消费的资产表示。KTX2 Spike 已验证代表容器的 Mip、sRGB、Alpha 与超级压缩元数据；生产级 GPU
块压缩仍依赖 Granit 扩充通用压缩格式、Footprint 与上传能力契约，不在本版本伪装为已支持。

## 本地验证

- Windows MSVC Shared Debug：完整构建及 134/134 测试通过。
- Windows Clang Shared Debug：严格警告完整构建及 134/134 测试通过。
- 安装 SDK 验收覆盖外部模板创建、Debug 模块构建、Development/Shipping 打包及包内 Runtime
  smoke；版本提升后相关 4 项安装与版本测试再次通过。
- 相同输入的第二次 Development 构建命中缓存并产生逐字节一致 ZIP；Shipping 能裁剪模板中的
  未引用资产，且不携带源文件、缓存或 Development 构建元数据。
- `git diff --check` 通过。

## 远端验证

- [Linux Actions 34043190243](https://github.com/synchronized/gneiss/actions/runs/34043190243)：
  Clang/GCC Shared/Static、Granit Runtime Shared/Static 和 Sanitizer 共 7 个作业全部通过。
- [Windows Actions 34043192252](https://github.com/synchronized/gneiss/actions/runs/34043192252)：
  MSVC Runtime Shared/Static 与安装 Consumer Shared/Static 共 4 个作业全部通过。

完整远端矩阵针对候选提交仅执行一次；本记录和状态更新不改变代码、构建、依赖或测试行为，因此不
重复触发 Actions。
