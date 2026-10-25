<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 网格与纹理异步加载实施记录

日期：2026-10-26。范围：[PLAN-052](../plans/PLAN-052-async-render-assets.md)。

## 实现结果

运行时加载服务使用 file_service 读取网格和纹理，文件等待不占 CPU 任务。
Texture 按描述读取、依赖解析、负载读取、内存解码推进；Mesh 整读后解码；
Material 从 asset_manager 领取描述后走同一个纯内存准备入口。
运行时已不再创建旧 asset_preparation 状态机。来源身份通过管理器解析，支持注入服务。
纹理有设备 profile 时保留选中变体和 Manifest，原始文件来源释放，GPU 恢复无需重新读盘。

描述复用现有纹理 JSON、容器 Manifest 和 GNMS 头部；没有新增重复的导入文件。
既有导入构建流程继续负责源文件/设置变化后的产物生成，本轮没有增加自动监视或重新导入能力。

## 验证结果

Windows Clang Debug，全工程及已启用测试目标编译成功。
最终构建日志：本地 build/asset-migration/async-render-assets-final2.log。
变更源码 clang-format --dry-run --Werror、Core 边界、公共接口边界及 git diff --check 通过。

新增人工延迟后端契约检查：注入服务、文件等待零 CPU 任务、描述/负载分阶段、
单次解码、GPU 确认前未就绪、失败保留旧内容、取消后迟到完成，以及 Mesh 同一路径加载。
现有纹理用例更新为 CPU 负载保活，内存夹具提供异步读取回执。
以上测试仅编译，未执行，不能据此宣称运行结果通过。

## 剩余范围

按用户要求未执行测试、大场景、GPU 场景或 CI。
未实现 mip/LOD 流送与显存部分驻留；整读临时输入和 CPU 常驻负载仍有成本。
显式同步工具加载接口继续存在；浏览器后端、完整桌面写入能力和异步宿主关闭归 PLAN-051。
