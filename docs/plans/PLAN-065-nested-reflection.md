<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-065：嵌套值对象反射

## 状态

已完成。

## 范围与决策

扩展成员属性绑定以识别独立注册的值对象，保持通用 value 的基础值集合不变。
对象成员只借用父对象内存，解码只修改临时父对象；不增加持久化对象所有者。
不实现指针图、容器、optional 或继承。数学类型不依赖 Meta，身份和注册均在外部定义。

## 实施与验收

- [x] 嵌套成员的类型元数据、访问器及冻结前身份匹配检查。
- [x] 反射 JSON 在同一文档中递归处理，深度上限 64；子 codec 需要合法 JSON 对象。
- [x] 校验按子类型执行，错误路径包含完整父路径；失败不向调用方暴露临时对象。
- [x] 独立登记 camera_settings 参数与校验，Camera 直接绑定 settings 和 is_primary。
- [x] 仓库 Camera 文件改为 settings 下的 fov/near/far，未发布格式不保留旧字段兼容。
- [x] Debug 完整编译及相关短测试；不运行大场景、GPU、远端 CI。

World 运行期属性和 IPC 的现有字段协议不属于此次资产格式迁移。

## 验收记录

2026-10-29：Windows Clang Debug 完整编译、31 项 Meta/资源/Scene/Prefab/编辑器短测试通过。
新增检查覆盖多层往返、子类型缺失、默认成员值与非法默认值、嵌套错误路径、失败释放、
自定义子 codec 优先及非对象输出拒绝、64/65 层边界，以及旧 Camera 格式拒绝。
6 份修改的 JSON 资产和 3 个资产生成 Python 脚本通过语法检查；同时修正项目模板残留的旧组件字典布局。
未运行大场景、GPU、性能脚本、浏览器构建或远端 CI。
外部未发布场景需将 Camera 内容改为 settings + is_primary，不提供旧字段回退。
