<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：编辑器视口相机分离

日期：2026-10-16。

依据 [ADR-067](../decisions/ADR-067-editor-viewport-camera.md)，编辑器相机改为纯编辑器状态，
不创建或销毁 World 实体。宿主普通界面与加载/模态界面提交时设置视口相机；常规渲染和
截图均使用该值副本。World 渲染快照接受显式相机，未指定时保留运行时主相机路径。

回归覆盖：初始化不增加实体或抢占主相机；漫游和关闭不影响运行时相机；显式视角、
无场景相机、删除运行时相机后渲染输入、尺寸变化、无效输入不覆盖快照；宿主接口非法参数、
跨线程调用、清除和销毁后失效。Editor 场景加载测试继续覆盖场景切换。

Windows Clang Debug 的 Editor、相机、渲染快照与场景加载测试目标构建通过；7 项相关测试
（场景加载、编辑器相机、C/C++ Application、渲染快照、Core 和 World 服务边界）以及一次
Editor demo 冒烟通过。Core 负向依赖检查也通过。渲染快照 clang-tidy 完成，仍报告原有的
两处聚合初始化末尾逗号风格告警。

未运行 Linux、静态安装消费者或完整 Sponza 交互验收；未用图像比对验证截图。
不将本项描述为完整 Prefab 异步流程完成；多视口与相机预览尚未实现。
