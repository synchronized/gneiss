<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-124 场景 v4 Prefab 覆盖格式实施记录

## 结果

M-124 已把 Prefab 实例的稀疏字段覆盖纳入 `gneiss.scene` v4。每条覆盖由来源节点 UUID、Type ID、
Field ID 和显式类别的拥有型值组成；实例 UUID 由外层 Prefab 实例声明提供。

## 已验证行为

- 解析器拒绝非规范 UUID、零或非小写 Type ID、零或越界 Field ID、未知值类别、非法数值和重复键。
- 覆盖集合进入场景描述后按完整作者键确定性排序；序列化输出使用同一顺序。
- bool、整数、浮点、字符串、Type ID、vec3 与 quaternion 均有无歧义的 JSON 编码。
- 覆盖解析失败不会发布部分场景描述；未来版本与旧版场景分别返回明确错误。
- 当前项目尚未发布，仓库场景、生成器、示例和测试直接升级到 v4，不保留 v1—v3 迁移代码。
- v4 作者 JSON 的未知扩展字段继续随场景描述保存与输出。

## 验证

Windows Clang Debug 完整构建通过；场景描述、Prefab 描述和 Prefab 覆盖模型三个专项测试程序返回
成功。当前 CTest 配置未注册这些可执行测试，因此本阶段直接运行目标程序验证。

字段是否存在、类型是否匹配以及是否可写，需要冻结的 Type Registry，留给 M-125 的 Runtime
实例化和刷新事务处理。
