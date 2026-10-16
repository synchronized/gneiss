<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：内置组件与 Prefab 候选属性覆盖

Transform 与 Camera 绑定候选数据 setter，普通运行时 setter 复用字段赋值规则后仍通过 World
提交。Transform 校验有效四元数及非零缩放；Camera 候选先校验单字段范围，near/far 联合约束
在全部覆盖应用后检查，避免合法批量覆盖受中间值顺序影响。活动相机字段仍只读。

Prefab 增加私有描述准备入口：复制来源、验证覆盖身份与类型、调用候选 setter、完成联合校验，
成功后移交输出；失败保持来源和原输出不变。不读文件、不创建实体、不发通知。候选数据布局
按当前对象格式中的 Transform/Camera 提供，尚无自定义组件序列化容器，不将任意回调直接
转换成这两种内存布局。注册表候选扩展契约仍按 ADR-066 保留。

Windows Clang Debug 构建通过，component_reflection、prefab_runtime_instance、
world-service-boundary、core-boundary、core-boundary-negative 共 5 项测试通过。
新增验证候选位移、非法缩放保留数据、near/far 连续赋值、Prefab 来源不变及身份失配保留输出。

准备入口尚未由 Editor 异步放置控制器调用；发布后通知、取消与历史仍需串联。
未作真实窗口、GPU、性能或跨平台验收，0.51 未发布。
