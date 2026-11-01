<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：Editor 模态网格节点创建

2026-10-16，资产浏览器“添加网格节点”接入既有模态资产准备控制器。
交互决策与后续范围见[开发计划](../plans/DEV-051-architecture-boundaries.md)。

## 实施结果

已采用且就绪的资源直接创建节点，不闪现进度界面；缺失资源先异步准备，期间不添加节点或历史。
准备成功后通过内部 ready-only 接口创建场景节点，再采用资源版本；失败保留原场景和历史。
共享控制器沿用取消、窗口事件处理和文档有效性检查。

创建接口校验资源类型、所属服务和 URI，只接受实际租约，不回退到同步文件读取。
编辑器记录和场景作者描述在提交前分配；历史命令持有网格和材质租约，撤销释放节点，
重做复用这些版本，因此文件随后损坏也不影响该命令重做。

## 验证与限制

Windows Clang Debug Editor 及相关测试构建通过，定向 CTest 6/6 通过：
editor.asset-edit、editor.scene-loader、editor.command-history、scene-load-builder、
core-boundary、core-boundary-negative。

新增测试覆盖协作和线程池模式下的异步创建、准备期间节点与历史不变、坏文件后的撤销重做、
已就绪资源直接创建、零容量历史拒绝提交和准备失败；场景层覆盖错误资源类型、重复 UUID、
节点销毁和创建过程不读文件。

相关实现运行 clang-tidy，修正本轮控制器的尾逗号告警；既有实现仍有参数类型相似、
复杂度等告警，不宣称项目静态检查零告警。

Prefab、子树恢复及其他旧同步编辑入口尚未迁移。本轮没有真实窗口、GPU、完整 Sponza、
跨平台或安装矩阵验收，未执行远端操作，0.51 尚未发布。
