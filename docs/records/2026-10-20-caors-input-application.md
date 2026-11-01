<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 输入资源加载到 InputService 生效闭环

2026-10-20，承接[私有模块抽取](2026-10-20-caors-private-adapter.md)。

## 交付

- 强类型 action_map_asset 和 Loader 从测试程序移到 function/input。
- 新增 action_map_application，保存一个待应用的 CAORS 请求订阅。
- 宿主 begin 发起请求，推进 Loader 和完成队列后调用 poll(InputService)。
- 加载期间不替换映射；成功后复制不可变资源内容给既有 replace_action_map，最多应用一次。
- 新 begin 取消旧订阅；旧资源即使因其他订阅继续加载，也不会迟到覆盖当前选择。
- 失败、取消、销毁待应用对象均保留当前映射。后台不持有 InputService 指针。

begin 返回 success 仅表示成功建立待应用请求，不代表资源加载成功；
poll 在加载中返回 not_ready，终态结合 state 查询。
主动取消后 state 为 cancelled，poll 返回 invalid_state，重复轮询不会再次替换。
所有待应用对象的方法均由输入服务所属线程调用。

## 验证

Windows / Clang / Debug，固定 CAORS 提交与上一阶段一致。
相关 11 项回归通过，接入测试连续 20 次通过，新增闭环覆盖：

- 缓存命中与未命中缓存的真实异步加载均能应用。
- 初始旧 action_id 在成功替换后失效，重复 poll 不再次改变代次。
- 已加载物理键绑定实际产生 -1 / +1 动作值。
- 语义加载失败、取消、待应用对象销毁后，现有 action_id 和映射保持有效。
- 有其他订阅者继续加载的旧请求迟到完成，不覆盖最新映射。

## 当前边界

新增 gneiss_caors_input 私有静态库，仍由默认关闭的验证选项启用；只链接独立宿主，
不与 Engine DLL 同时链接，不安装也不新增公共 C/C++ API。
InputService 的真实实现已进入此验证闭环，但 application::load_action_map、Editor 启动和
生产资源服务尚未改用该路径，不能宣称编辑器已全面异步加载输入映射。

下一阶段接入 application 的创建、主循环推进及退出顺序，并决定公共异步调用入口的契约。
Linux、跨 DLL、分配失败注入与真实编辑器交互仍未覆盖。
