<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：渲染消费者迁移

日期：2026-11-11。

## 变更

asset_pixel 和 main_loop_response 不再使用旧场景请求、阶段枚举或 scenes().activate。
两者使用资产请求取得固定内容，再组合、实例化；测试辅助只封装重复装配步骤，
返回实例化回执和视觉批次，不隐藏等待或替换策略，不增加生产服务。
像素测试保留视觉就绪前后差异、缓存复用、取消保持旧场景及旧句柄失效断言。
删除仅属于旧进度结构的 texture_payload_bytes 零值断言。
响应基准的 scene_ready 现在表示实例化及视觉批次均完成；因此加载阶段采样时间
可能比旧口径更长，旧基准结果不可直接按相同阶段比较。未提供 GPU 上传完成保证。

定向回归暴露此前结构重载测试的日志计数竞态：日志回调在独立线程执行，
额外调用 Application.run 不能充当日志屏障。改用互斥量和条件变量等待实际回调。

## 验证

Windows Clang Debug 全部默认构建目标通过，日志同步修正后相关目标重新编译通过。
五项定向 CPU 回归通过：native-application、native-application-flow、runtime.scene-loader、
scene_structural_reload、prefab_structural_reload；其中结构重载修正后连续 15 次通过。
源码边界检查与 git diff --check 通过。
未执行 GPU 像素测试、窗口响应基准、大场景或远端 CI；其运行行为仍待最终验收。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 仍有 scene_load_application
及 scene_load_baseline 两个旧消费者，旧服务与 Application 装配尚未删除。
