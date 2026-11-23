<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-125：统一属性访问与类型操作

状态：已完成，包含调用方 API 收敛修订。

## 已确认目标

成员和 getter/setter 注册统一生成 property_ops，不公开属性模式枚举；type_ops 管理原生值生命周期。
保留 instance 的借用语义，不扩展 value 为任意对象容器，不更改持久化格式。
访问器上下文仍借用，必须覆盖注册表和属性句柄的使用期；注册表复制操作表，不接管业务对象。

## 实施与退出条件

- [x] 直接成员统一地址访问，删除标量成员专用转换。
- [x] 原生临时值 RAII 与统一 getter/setter；按值 getter 不泄漏临时借用。
- [x] 序列化和快照支持按值对象 getter；完整替换和局部候选编辑语义明确。
- [x] 手工注册、测试、文档迁移，删除 property_binding 旧协议。
- [x] 相关生命周期测试及 CPU 回归通过。

## 语义与验收

只读属性不能借用写入，setter 属性不可通过直接地址绕过约束。类型构造、复制能力缺失时返回
unsupported；异常沿既有边界传播。临时值构造失败不析构未初始化对象，过度对齐存储正确释放。
getter 返回值的序列化只在临时对象存活时遍历。局部编辑先复制当前值，完整解码使用新候选。
覆盖直接成员、计算标量/对象/容器、只读、setter 拒绝、不可复制类型与异常生命周期。

## 实施结果与验证

- property_ops 统一直接成员、只读 getter 与受约束 setter；旧 property_binding 和专用 value 编码桥接已删除。
- 临时存储对常用小值使用栈内空间，较大值和过度对齐类型使用独立分配；不改变 instance 借用所有权。
- JSON、通用序列化、默认值校验和 value 捕获/恢复统一使用同步实际值访问。
- instance::assign(instance) 支持同类型原生复制；类型不匹配返回 invalid_argument。
  材质编辑器在自己的 replace_object 入口明确限制对象类型，不依赖通用复制接口拒绝标量。
- Windows Clang Debug 共享库完整构建通过；CPU 回归首轮 213/214 通过，材质编辑器边界修正后
  唯一失败项复测通过，共 214 项完成验证。计算属性用例覆盖对象/容器往返、快照恢复、部分修改、
  setter 拒绝、异常释放、过度对齐及不可复制类型。
- git diff --check 通过。本轮未运行 MSVC、静态库、其他平台或 GPU 验证。

计算属性回调的借用不可逃逸；候选使用 C++ 复制语义，含 ref 的对象不会自动深拷贝。

## API 收敛修订

用户确认将临时访问和候选协调收回内部，不以公开回调换取内部结构简单。
保留现有 get/member → read_value/assign 使用方式，不新增重叠的属性操作接口。
instance 内部仍可定位宿主属性，但不拥有业务对象；不向调用者暴露候选操作。
私有 instance_access 供 Meta 路径操作与序列化使用，不提供任意 C++ 拥有型容器。

- [x] visit_value、edit、replace、requires_replacement 收回私有协议。
- [x] read_path/write_path 直接表达按路径读写，内部管理 getter 临时值和沿途 setter。
- [x] 编辑器和 Prefab 的路径赋值接入；测试使用业务操作，不访问私有回调。
- [x] 保持整体解码、部分修改、失败不调用上层 setter 与临时生命周期验证，完成构建回归。

修订验收：Windows Clang Debug 共享库完整构建通过，214/214 项 CPU 测试全部通过。
路径用例覆盖多层计算属性、内外 setter 拒绝、异常清理、不可复制类型及持久化路径限制。
git diff --check 通过；未运行 MSVC、静态库、其他平台与 GPU 验证。
