<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 JSON 入口统一

## 实现

JSON 文本入口为 encode/decode，字节编码为 encode_bytes；视图与擦除入口为
encode_instance/encode_object/decode_object，写入已有视图为 read_into。
read_into 返回 result<void, diagnostic>，与格式无关入口一致；内部 typed_decode 共用结果判断。
资产、组件描述、编辑器和全部测试调用已迁移，旧 JSON serialize/deserialize 名称删除。
资产文件包装层保留自己的文件接口，没有混改文件协议。

Prefab 组件测试更新领域边界：类型不匹配的描述可通用编码但领域校验拒绝；
engaged optional 内含空引用仍因与缺失值的 null 歧义而被格式层拒绝。
读写 context 拆分仍待后续实施。

## 验证

编辑器、全部受影响 CPU 测试目标编译通过；渲染资产加载/准备测试仅编译，未运行 GPU。
16 项定向 CPU 测试通过：serialization、serialization-archive、reflection-json、meta-instance、
component-description、type_registry、type_builder、meta-container、meta-type-ops、component-adapter、
scene_description、action_map、editor.material-editor、prefab_property_override、editor.prefab-component、
assets.material。
验证涵盖 setter 约束、隔离注册表、失败诊断、快照、场景/Prefab 与材质格式。
日志：build/plan108-json-api-build.log、build/plan108-json-api-consumers.log、
build/plan108-json-prefab-test-build.log。
git diff --check 通过。未执行大场景、GPU 或远端操作。
