<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-119 测试分类、白盒依赖与安装验收

## 范围

2026-11-18 在 feature/0.52-architecture-cleanup 完成三项定向审查，不改变生产 API，
不调整测试超时、像素阈值或启用条件。以下结果不代表整个版本发布验收完成。

## 测试分类

Windows Clang Debug 共享、静态均枚举 233 项：213 项 cpu、20 项 gpu，
180 项 integration、46 项 unit、20 项 death；标签可重叠。无空标签及 cpu/gpu 冲突。
标签就近放在 add_test 所在 CMake 作用域；未用目录名称、排除 gpu 或全局默认值自动归类。
条件启用的 profiling-capture、Lantern Runtime 流程也补充分类，但本轮未运行这些未启用配置。

- Engine 无窗口 Application、资源服务和模拟执行器不创建 GPU，归 cpu。
- Editor 的 ImGui 布局/面板模型使用测试上下文；不因链接 UI 就归 gpu。
- 实际启动 Runtime 的进程、资产工作流、安装 smoke，以及原生窗口和像素测试归 gpu。
  隐藏窗口仍创建渲染后端。runtime.startup-diagnostic 还启动有效工程以验证缺场景失败，不能只当参数检查。
- Lantern 普通编辑/Prefab 工作流是无窗口宿主，归 cpu；启动真实 Runtime 的流程独立归 gpu。
- 致命回调、分配边界、线程契约和断言子进程加 death；unit 只用于已核查的局部值/容器行为，
  跨模块、文件、进程及装配流程保留 integration。

## 白盒目标

四个链接 Engine 的目标移除 18 份重复源码编译：render_asset_loader、texture_load_service、
asset_material、resource_service。URI、Mesh/Texture binary、KTX2、PNG、SHA256 的所需符号
已有 Engine 导出，改由生产库提供；共享、静态完整构建及对应行为用例验证了链接闭包。
删除 Lantern 工作流过时的 src 包含根，不新增接口库、扩大 DLL 导出或增加共享注册表副本。

保留以下本地实现编译，并区分其理由：

| 类型 | 保留原因 |
| --- | --- |
| 私有 asset_decode、resource_cache、render executor/scene/frame_packet、World hierarchy | 直接验证非导出实现，不能为删测试源清单扩大公共边界 |
| PNG/value 分配失败、独立 logging 实现 | 需要同一可执行文件分配拦截，或独立于 Engine 的模块验证 |
| 独立 binary/数学/Input/Files 目标 | 使用最小模块验证，部分不链接 Engine；不能删除其唯一实现 |
| Editor/Runtime 组件注册测试的私有包含根 | 注册测试组件需要受限内部访问，路径为 PRIVATE，不传播给生产消费者 |

未发现测试通过 get_target_property(... SOURCES) 或 TARGET_PROPERTY:SOURCES 借用其他测试的源清单。
Granit/libuv 第三方构建中的源清单操作不属于本次测试清理，不修改。

## 新发现的旧断言与 GPU 缺口

扩大 CPU 集合暴露 ui-layout 首帧断言仍要求透明 DockSpace。生产 begin_editor_workspace 已明确
使用离屏 Scene View，不再透出窗口场景；测试改为验证节点存在且没有透出标志，保存、恢复、
重置和失败行为断言不变。没有为了通过测试恢复旧界面行为。

两个历史 GPU 用例初次仍失败。同步 load_scene 测试辅助只实例化，不选择宿主主相机；
像素夹具现显式枚举唯一相机并调用 set_active_camera，不修改通用实例化职责。
修正后 pbr-negative-scale 在共享和静态配置均通过，原有可见性和法线差值阈值不变。

asset-pixel 仍失败：已不再是背景色，首个纹理重载用例等待初始红色时实际 R=233、B=233。
等待循环现在输出期望与实测颜色，并驱动宿主更新，不只重复回读。仍须定位纹理绑定/采样链路，
不能据此认定是 Granit 缺陷；本轮没有修改后端、第三方或减弱像素断言。

## 验证证据

| 项目 | 结果 |
| --- | --- |
| 共享、静态 Windows Clang Debug 完整构建 | 白盒源清单收敛后均通过 |
| 共享 CPU 首次集合 | 211/212；仅 ui-layout 失败。当时 Lantern 编辑工作流尚在 GPU 集合 |
| 静态 CPU 首次集合 | 212/213；仅 ui-layout 失败 |
| 修正后定向复测 | 两种配置的 ui-layout、Lantern 编辑工作流均通过；不将首次失败改写为全绿 |
| GPU 初次 | asset-pixel、pbr-negative-scale 均失败 |
| 相机修正后 | pbr-negative-scale 共享/静态通过；asset-pixel 共享仍失败，具体实测见上文 |
| 静态指定前缀安装 | 当前产物安装通过，启用 CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION |
| 静态独立包消费者 | find_package、链接和 error::message 调用通过；借用仓内声明，不代表提供安装头文件 SDK |
| 已安装 Runtime | 共享/静态 gneiss.runtime.installed-smoke 各 1/1 通过，含安装与脚本所定义的打包流程 |

静态包旧阻塞是仅传 Gneiss 前缀。复用已有 build/granit071-static-prefix（Granit 0.71.0）
与当前 build/audit-static-install 后配置成功；没有绕过 find_dependency 或修改第三方仓库。
消费者源在 build/audit-package-consumer，构建在 build/audit-package-static；安装 Runtime
使用仓库已有 check_installed_runtime.cmake。日志位于本地 build/three-items-*.log。

未执行 MSVC、Release、Linux、Web、全 GPU 集合、压力/回收预算和设备丢失测试。
不推送、发布或把此记录当作 PLAN-115/119 全部完成。
