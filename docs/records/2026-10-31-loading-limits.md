<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资源加载额度简化实施记录

日期：2026-10-31。对应 [PLAN-095](../plans/PLAN-095-loading-limits.md)。

## 交付

删除加载 options、Decoder 注册、asset_manager 构造、resource_service 与场景请求组的额度参数。
删除缓存身份中的读取大小，保留纹理 profile。文件服务新增使用配置上限的 read 重载。
移除网格、材质与纹理采用阶段的通用额度判断，保留格式验证、统计溢出检查与 PNG 输出保护。
当前边界见[资产参考](../reference/assets.md)。

## 验收

- Windows Clang Debug 编译通过；开发警告作为错误。
- 16 项相关测试通过：assets.manager、files.service、assets.material、files.native、
  png_decoder、action_map、scene-load-preparation、resource_service、texture_ktx2、texture_binary、
  render_asset_preparation、render_asset_loader、texture-load-service、scene-instantiation、
  serialization、reflection-json（均带 gneiss 前缀）。
- 512 MiB 文件服务配置被加载请求原样传到测试后端；仅返回一个字节，没有大内存分配。
- 收紧文件服务上限时仍拒绝超限读取，原有 PNG、容器格式与溢出测试继续通过。
- 修正场景准备测试遗留的强缓存假设：原始描述释放后弱缓存失效，持有引用时复用。
- git diff --check 通过。未运行 GPU、大场景、浏览器或远端 CI。

## 限制

文件服务仍按单文件上限预留读取预算；宿主须协调 max_file_bytes 与 max_buffer_bytes。
并发解码、CPU 驻留与 GPU 数据没有统一总内存预算，后续由流送设计处理。
