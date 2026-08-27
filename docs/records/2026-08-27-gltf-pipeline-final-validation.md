<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 2026-08-27 glTF 资产链与渲染优化最终验收

## 结论

TOOL-001、TOOL-002、TOOL-003 及其后续渲染优化已形成可评审闭环：glTF/GLB 由离线工具转换为
Gneiss Runtime 资产，运行时加载 Mesh Binary v1 并保留索引绘制；逐对象数据通过动态 Uniform
Offset 提交，静态几何由持久 Arena 复用。核心安装包不传播 fastgltf，Granit `PACKAGE` 与锁定
提交的 `FETCH` 接入路径均已验证。

## 本地验证矩阵

验证环境为 Windows、Clang 22.1.8 与 Ninja。所有配置均启用警告并将项目警告视为错误。

| 范围 | 链接与配置 | 结果 |
| --- | --- | --- |
| 核心、工具与安装后 Consumer | Release Shared | 35/35 测试通过，C11/C++20 Consumer 通过 |
| 核心、工具与安装后 Consumer | Release Static | 35/35 测试通过，C11/C++20 Consumer 通过 |
| Granit `PACKAGE` | Release Shared | 33/33 测试通过 |
| Granit `PACKAGE` | Debug Static | 33/33 测试通过 |
| Granit `FETCH` | Release Static | 39/39 测试通过 |

`PACKAGE` 静态验证使用与本机 Granit 安装包一致的 Debug 配置；`FETCH` 静态验证使用 Release，
并从仓库配置的远端获取锁定提交 `d5aa1cceef0741c17ff58eac5f14f731a3991bcb`。关闭工具构建的
`PACKAGE` 两组配置均未配置 fastgltf，验证了工具依赖隔离。

## 覆盖能力

- glTF/GLB 校验、静态 Mesh 解码、Transform 与材质映射、多 Primitive 拆分及外部资源边界。
- 确定性、事务式 Runtime 资产写入，以及 Mesh Binary 检查、读取与失败路径。
- Lantern Gallery 的离线导入、场景实例化、纹理加载和图形冒烟。
- 索引所有权、越界校验、Mesh Binary 索引保留及 Granit Indexed Draw。
- 动态 Uniform Offset 对齐、容量与设备限制，以及同 Mesh 多实例提交。
- 静态几何 Arena 复用和 Mesh RID generation 失效后的重新上传。

## 平台验证边界

Windows 托管 Runner 没有 Vulkan ICD，因此远端 Windows 矩阵只验证非图形用例；真实窗口、
Granit 平台和图形示例由 Linux Actions 的 Xvfb、Weston 与软件 Vulkan 环境负责。
