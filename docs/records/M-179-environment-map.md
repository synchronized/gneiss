<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-179：Environment Map 最小闭环

## 结果

工程格式 v3 可声明单个环境资产、强度和水平旋转。Editor 与 Runtime 通过 Application 配置把
环境 URI 交给引擎；引擎先经 VFS 读取已导入载荷，再由渲染线程创建 Granit Environment Map。
未配置资产时使用 Granit 内建中性环境。Lantern Gallery 已使用工程级强度和旋转配置。

环境资产属于派生缓存，工程作者接口只引用 `.gneiss-environment` URI，不出现 Granit `.grenv`、
句柄或头文件。Application C ABI 使用指针加长度传递 URI，并明确校验有限强度、旋转和保留字段。

## 失败与生命周期

- 工程解析会拒绝资产根之外或不存在的环境 URI，以及负强度和非有限数值。
- 已导入载荷无效时，渲染线程销毁未完成投影并回退内建环境，不阻止场景运行。
- Environment Map 与 Render Pipeline 都仅由渲染线程创建、借用和销毁。
- 当前工程环境配置在 Application 创建时生效；修改环境配置需要重启 Editor/Runtime。动态替换会在
  环境导入器和资产类型进入正式热重载协议时实现，避免把 Granit 私有载荷伪装成作者格式。

## 验证

- 工程描述测试覆盖 v3 环境 URI、强度、旋转和路径存在性。
- C11 头文件测试覆盖 Application 描述 v6 的稳定布局。
- Runtime、Editor、Lantern Gallery 和 Granit 平台冒烟覆盖内建环境路径。

## 已知边界

- 本版本不提供 HDR/EXR 环境导入器，也不提交示例专用的大体积环境载荷。
- 环境状态与回退信息由 M-180 纳入退出诊断。
