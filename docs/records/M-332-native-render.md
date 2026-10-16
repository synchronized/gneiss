<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：原生绘制列表、快照与宿主提交

2026-10-16，按 DEV-051 的 Render 服务接口阶段实施。

## 已实现

UI 顶点、命令与调试线段使用 Render 自有值类型；业务层不再保存绘制 C 结构的保留字段。
API 适配检查布局、数量上限、保留字段与深度标记，并逐字段转换；语义、RID 与索引范围继续
由 Render 检查。失败保留原列表；帧包使用新类型计算复制字节数。

Editor 网格和 ImGui 直接生成原生绘制数据，通过宿主借用的 application 对象提交，
不再每帧调用 SDK/C 绘制接口；未导出 registry 解析函数供编辑器随意使用。
宿主仍拥有 application，借用指针仅在 run 回调期间使用，不新增运行时对象。

C 消费入口暂需转换数组后再由列表复制，因此存在额外临时存储；原生 Editor 路径没有这次
ABI 转换。该成本尚未做大型场景测量，不把重构描述为性能优化。

## 验证

Windows Clang Debug 构建成功，9 项绘制列表、帧包、C/SDK 提交、编辑器网格和宿主启动回归通过。
追加 ABI 保留字段拒绝、深度标记拒绝、转换拥有存储和失败保留输出测试。
对两个绘制列表实现执行 clang-tidy，无项目诊断；完整渲染图像、Linux、静态安装矩阵未验证。

## 原生相机与实例快照

用户确认 ADR-062 方案 A 后，相机参数改用 Render 的 camera_settings，World 复用同一个
定义；TRS 改用 Core 的 engine::transform。World 现有 C 组件在快照生产处逐字段转换，
Editor 直接从相机值构造原生视口输入。活动相机选择仍属于 World，快照不保存 C 保留字段。

model/normal 数学由 Granit 目录移至通用 Render，测试不再要求 Granit 后端或链接 Granit。
相机矩阵仍由 World 调用通用算法准备，实例矩阵仍在渲染线程准备；没有增加主线程全场景工作。
矩阵输出复用时先清空旧元素，补负缩放、零缩放拒绝与失败保留输出验证。

完整 Windows Clang Debug 构建通过，包含 C11/C++20 公共头编译目标。
16 项相关回归通过，覆盖相机数学、快照、帧包、队列、World、C/SDK Application、宿主冒烟、
编辑器相机及原生窗口交互。完整构建发现上一轮窗口测试仍调用旧 ImGui 提交接口，已迁移为
原生 Application 拥有者与借用桥，并通过 18 次 resize、3 次最小化恢复、5 次模拟 DPI 检查。
模拟 DPI 不等于真实跨显示器验证。

新增通用快照/数学边界规则及违规反例；追加后 3 项数学与边界测试通过。
camera_math、scene_projection_math 的 clang-tidy 无项目诊断。此次未做 Sponza 图像对比、
性能测量、Linux 与静态安装矩阵，不将迁移描述为渲染性能改善。

## 材质与采样原生值

2026-10-16，资产解析和 Render 共用内部材质枚举与逐槽采样类型；double_sided 使用 bool。
C 入口逐字段转换并拒绝非法布尔值，原生服务独立拒绝未知枚举，旧版本材质描述的缺省行为保持。
Granit 的数值转换只在后端适配处发生。normal_math 也已使用原生 TRS 与数组法线。
完整 Windows Debug 构建通过，11 项资源、材质、资产准备、加载、帧包、SDK 所有权、法线与
边界回归通过。新增原生路径未知枚举拒绝测试；未重复运行完整图像或跨平台矩阵。

## 内部 RID 定义

资源表改用 Core 自有 64 位 RID 定义；Render 资源服务、帧包、快照、资产租约与后端资源
映射使用相同定义。C ABI 编码不变，仍由唯一资源表校验类型、generation 和所属服务。
这不是编译期强句柄隔离，也没有新增资源管理状态。
完整 Windows Debug 构建通过，9 项 RID、资源、材质、快照、帧包、加载、SDK 所有权和边界
回归通过；World 快照头补上自己实际需要的 World 声明，避免依赖 Render 的间接包含。

## 剩余

网格顶点与纹理格式等接口尚未原生化。
Render 尚未完成，Application 回调与上层调用链也未完成。
