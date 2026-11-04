<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Scene/Prefab 作者描述与运行时覆盖编辑分离

2026-10-21，0.51 分支；对应[模块布局计划](../plans/PLAN-051-module-layout.md)批次 B。

## 改动与边界

- scene_description 与 prefab_description 的头文件和解析/序列化实现迁到 engine/asset。
- 新的 asset/prefab_property_data 保存作者地址、稳定属性键、自有属性值、UUID 校验、
  键比较与值类别查询。这些实现从旧文件迁出，不增加第二套数据。
- function/scene/prefab_property_override 仅保留依赖 Registry 的字段校验与稀疏覆盖编辑。
- Scene/Prefab 描述不再包含 property_registry；运行时实例化与编辑代码显式包含操作接口。
- 描述往返测试不再编译运行时覆盖编辑实现；更新内部头清单与所有构建源码路径。
- 新增边界负向用例，拒绝描述层包含 Registry、World 或功能层覆盖接口。

保留 scene_internal 领域命名空间，未创建旧路径兼容头或类型别名；本批按职责迁移文件，
不扩大为全部调用点的命名空间重命名。最终 resource 目录合并仍按计划另行实施。
Scene Tree、ECS 与 Prefab 实例化继续由 function 层负责，不迁入资产层。

## 验证

Windows Clang Debug 下相关目标与 Editor model 构建成功。11 项 CTest 通过：

- Scene/Prefab 描述、属性覆盖、Prefab 实例、Scene Prefab 实例、结构重载。
- Scene 加载候选、Scene 异步准备、Prefab 异步准备。
- core-boundary 与 core-boundary-negative。

clang-format 与 git diff --check 通过。对新作者数据实现及原覆盖编辑实现运行 clang-tidy，
报告 4 项迁移前已有逻辑的建议/警告：ranges 算法、UTF-8 校验复杂度，以及两处
noexcept 中 std::visit 的异常逃逸分析。未通过关闭规则规避，日志在
build/scene-resource-tidy.log；本批不宣称静态分析零警告。

未执行完整静态/共享安装矩阵、Web、Linux 或 GUI 手工验收；公共 ABI 未改变。

## 剩余工作

描述与操作分离不等于 Scene/Prefab 已接入统一 CAORS 缓存。Prefab loader、渲染租约、
纹理上传通道及资产管理器中的渲染配置仍待后续批次收敛；批次 B 尚未整体验收完成。
