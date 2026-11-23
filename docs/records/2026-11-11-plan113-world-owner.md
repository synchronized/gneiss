<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：Application 直接拥有 World

日期：2026-11-11。第三组的所有权迁移，完整计划尚未完成。

## 变更

Application 私有存储直接拥有 World；渲染提取和 native_world 不再通过 scene_service 转发。
关闭时先停止场景协调，再释放 World，最后关闭资产及渲染依赖。
scene_service 只借用 World，删除 active_/retired_ World 存储和候选 World。
准备阶段只取得资产和组合内容；激活时在既有 World 创建场景实例，实例化失败回收本次场景，
成功后卸载上次由该操作创建的实例，其他追加场景保留。默认相机提示在激活时应用。

## 验证

Windows Clang Debug 全量构建及修改后增量构建通过。
6 项定向回归最终通过：assets.application、scene-load-application、scene-load-preparation、
texture-load-application、scene_instance、prefab-runtime-application。
场景测试新增 World 地址稳定、无关场景保留、被替换场景失效检查；旧的候选资源身份、
World 指针更换和退休等待断言按新契约调整。分层检查与 git diff --check 通过。
未运行完整 GPU、大场景、跨平台或远端 CI。

## 剩余

scene_service 仍承载打开/重载请求和来源基线，尚不能删除；退休统计接口暂留供监测调用方迁移，
但已没有待退休 World。后续迁移宿主策略和来源协调，再删除旧接口并移动 application 目录。
