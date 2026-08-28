<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# API 稳定级别与兼容策略

## 当前状态

Gneiss 仍处于 1.0.0 开发阶段。0.x 版本的公共 API、ABI 和构建契约尚未冻结；当前导出符号基线只
用于发现意外变化，不代表所有符号已经稳定。1.0.0 发布候选会冻结首份 Stable 清单。

## 稳定级别

| 级别 | 含义 | 1.x 兼容承诺 |
| --- | --- | --- |
| Stable | 经过代表性样例和跨版本测试的运行时契约 | C ABI 二进制兼容；C++ 包装源码兼容 |
| Experimental | 可公开试用但仍可能演进 | 提供迁移说明，不承诺 ABI 或源码兼容 |
| Internal | 仅供仓库内部实现使用 | 无兼容承诺，不安装也不导出 |

仅安装头文件或导出符号不等于 Stable。具体能力以本页和 1.0.0 发布清单为准。

## 1.x Stable 规则

- 已发布 C 符号、结果码和常量值不删除、不重命名、不改变含义。
- 已发布结构字段不重排或改型；可扩展结构只在尾部追加字段，并接受旧 `struct_size`。
- 不改变字符串、数组、回调、句柄及资源的所有权和有效期。
- 不让异常、C++ 类型、第三方类型或平台宽度不固定的类型穿过 C ABI。
- C++20 包装保持调用源码兼容，但调用方更换编译器、运行库或标准库后必须重新构建。
- 弃用至少跨一个次版本保留；移除 Stable 能力只能发生在新的主版本。

## 持久格式

场景、工程、动作映射、资产索引和 Mesh Binary 各自携带格式版本。库版本升级不会自动改变这些格式
的兼容范围；读取、迁移和拒绝规则以对应格式 Reference 为准。

## 当前候选范围

M-65 审计将以下能力作为 Stable 候选，而不是已冻结承诺：

- Core 版本、结果码、实体 ID 与 RID 值语义。
- Application 生命周期、回调平台、World 与基础 Entity 生命周期。
- 输入快照和动作映射运行时查询。
- Scene Tree、Transform、场景运行时加载与查询。
- Mesh、Texture、Material、Camera 与 Mesh Renderer 的运行时路径。
- 资产 URI 校验和安装后的 `gneiss::gneiss` CMake target。

以下能力默认保持 Experimental：Type Registry 与属性访问、场景作者修改与序列化、UI/Debug Draw、
Granit 平台选择细节、Editor、`gneiss_assetc` 及其工具 SDK。代表性样例或审计可以缩小候选范围；扩大
Stable 范围必须补齐同等级跨版本测试。

决策依据见 [ADR-023](../decisions/ADR-023-public-api-stability.md)，本次审计结果见
[M-65 公共 API 审计记录](../records/M-65-public-api-audit.md)。
