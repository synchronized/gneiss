<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：场景替换策略归还宿主

日期：2026-11-11。第三阶段继续实施，完整计划尚未完成。

## 变更

scene_service::activate 仅追加场景，不根据来源基线自动卸载上次实例；shutdown 放弃请求和
来源状态，不卸载 World 内的实例。Application 最终释放 World 时统一销毁其剩余成员。
Runtime 场景加载器保存当前场景借用句柄，在新实例创建成功后显式卸载旧实例。
编辑器沿用 scene_edit_session::close 的卸载入口；基准和像素测试中的切换也显式卸载旧实例。

## 验证

Windows Clang Debug 全量构建与最终受影响目标构建通过。
8 项定向测试最终通过：editor.scene-loader、native-application、scene-load-application、
scene-load-preparation、scene_instance、prefab-runtime-application、editor.asset-scene-commands、
editor.document-editing。旧的自动替换计数断言改为追加语义；编辑器旧 World 地址变化断言改为
地址稳定和旧实例/对象失效。分层检查与 git diff --check 通过。
未执行窗口像素测试、大场景基准、跨平台或远端 CI；监测程序已完成编译。

## 剩余

scene_service 仍有打开请求、视觉完成汇总及最后一次激活的来源重载基线。
本次没有将其描述为多场景来源重载服务。后续将来源基线交给宿主，迁移直接资产加载、组合和
实例化调用，再删除旧 open/activate/reload 协议。
