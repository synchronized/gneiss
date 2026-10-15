<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：节点编辑保留未改变的资产版本

2026-10-15，检查同步消费路径时发现：set_mesh_renderer 每次重新按 URI 获取两个资产。
节点持有固定版本、而同 URI 旧缓存仍存在时，即使输入 URI 不变，也会回退到旧缓存版本。

## 修复

网格和材质分别比较节点当前租约的 URI。未改变的选择直接复制该租约，只有改变或没有
租约的一侧才调用现有加载入口。两侧均成功后再写 ECS、作者描述及节点租约。
编辑选择不隐式执行热重载，显式异步重载仍由宿主版本事务负责。

没有建立额外的最新版本表，没有修改公共接口。新 URI 的获取、同步创建及同步热重载
仍走旧 URI 路径；本次仅修复编辑未改变选择时错误回退，不能据此宣称同步链路已经迁移。

## 验证

Windows Clang Debug 相关目标重建，定向 CTest 6/6 通过：scene-load-builder、scene_instance、
scene_structural_reload、scene-load-application、core-boundary、core-boundary-negative。

在固定绿色材质与同 URI 旧缓存蓝色材质同时存在的夹具上验证：

- 相同 URI 编辑保留原 Mesh/Material RID，文件读取计数不变。
- 请求缺失材质失败，原 ECS 引用保持不变。
- 仅修改材质 URI 后，网格仍是节点原先选定的固定版本。

场景服务执行 clang-tidy，新增方法改动无新增告警，原有复杂度等告警仍存在。
格式和 diff 检查通过。本次未重跑 GPU、完整 Sponza、跨平台或安装矩阵，未执行远端操作。
