<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-056：资源值结果与诊断统一

## 状态

实现完成（2026-10-29）。Windows Clang Debug 完整构建通过，44 项相关短测试通过。
未运行大场景、GPU 图像验收或远端 CI。

## 目标与边界

资源创建、解析和编解码直接返回 `result<T, diagnostic>`；错误字段统一为 `error`。
消除对外数据／诊断输出参数，保留纯校验 error、借用 getter 及原地修改接口。
文件格式不变；不增加缓存、版本协议或新的所有者层。

## 实施与验收

- [x] Mesh Binary、KTX2、PNG 编解码值返回。
- [x] Mesh／Texture 解析返回现有数据对象；JSON 文档解析返回拥有者结果。
- [x] Scene／Prefab／Material／ActionMap 编解码及描述序列化值返回。
- [x] Meta 编解码回调、Builder、模板接口与 Asset Decoder 全链路迁移。
- [x] 内建 Registry 工厂及诊断字段统一，删除旧接口及失效注释。
- [x] 完整本地编译、相关短测试；不运行大场景、GPU 图像验收或远端 CI。

失败仅返回错误信息，不交付部分数据；像素或诊断文本分配失败转换为 `out_of_memory`。
反射解码的类型擦除沿用共享所有者，用运行期类型 ID 确认类型，不增加第二套注册表。

## 验收记录与限制

- `cmake --build --preset windows-clang-debug -j6` 通过。
- CTest 的 44 项短测试覆盖 Binary、PNG、KTX2、Meta、资源加载、Scene／Prefab、编辑器保存与文档，以及分层边界。
- 新增仅可移动且不可默认构造、不可移动赋值类型的反射／Asset Decoder 验证；保留格式错误、错误位置、失败不交付数据与生命周期检查。
- PNG 持续故障注入针对实际像素缓冲和诊断文本分配。MSVC Debug 的标准容器代理可能在 `noexcept` 构造／移动中分配，因此不声称支持该标准库下“所有分配永久失败”的恢复；未关闭 Debug 迭代器检查。
- 保留纯校验、字段填充与原地修改的状态返回，以及无错误协议的普通查询／调试文本接口。未保留旧的资源编解码输出参数重载。

类型编解码的当前契约见[反射参考](../reference/reflection.md#类型编解码)。
