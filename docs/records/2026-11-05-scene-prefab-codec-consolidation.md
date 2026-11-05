<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Scene/Prefab 格式包装收敛验收

Scene v5 与 Prefab v1 改用通用 reflected_asset_codec，删除专用 parse/serialize、
encode/decode 转发及 decoded_object_document。Meta 增加文本便捷入口，编辑器、工具与测试
改用同一序列化路径。Prefab codec 文件已删除；scene_codec 保留组件注册和覆盖数据编解码。

整体 UUID、层级、主相机和 Prefab 根节点校验移至 scene_validation，通过类型校验注册调用。
局部字段校验继续属于各类型。删除没有生产消费者的 source_schema_version 和 Prefab
dependencies；运行时依赖仍由已有组件依赖收集路径获取。

按用户确认统一版本错误码：非当前正整数版本返回 unsupported；缺失、零、负数和非整数
版本返回 invalid_argument。旧 Scene v4 和未来 Action Map 版本的错误码随之调整。
重复格式头与旧 prefab_instances 字段仍明确拒绝，未知普通字段策略保持不变。
当前接口与规则见[场景格式](../reference/scene-format.md)及
[描述编解码入口](../reference/assets.md#描述编解码入口)。

验证结果：

- Windows Clang Debug 全量构建通过，包含 Engine、Editor、工具、示例与测试目标。
- 19 项相关测试通过（2.80 秒），覆盖 Scene/Prefab 往返与错误、版本策略、层级、组件、
  Prefab 覆盖、场景准备、编辑器操作、导入器以及通用反射序列化。
- 核心分层检查、改动源码 clang-format 和 git diff --check 通过。
- 源码、测试和构建配置未残留已删除编解码入口及来源版本字段。

未运行大场景、完整测试矩阵或远端 CI；未改变二进制资源协议。
