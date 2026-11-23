<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# JSON 节点 codec 收敛验收

资源格式包装与 engine::value 改用 JSON 节点回调。文件边界解析/输出一次，格式头校验和字段
遍历复用同一文档，不再复制整份文档后输出文本、重新解析。纹理的独立文件入口复用同一节点访问。
原有文件版本、字段表达、严格字段检查和业务校验保持不变。

节点回调借用当前注册表、引用环检测状态和递归深度；反射字段入口跳过本类型 codec，避免自递归。
删除已无调用方的 serialize_reflected/deserialize_reflected 字节入口及 value 的字节转换函数。
普通字节 codec 扩展保留；自定义字节 codec 嵌入 JSON 时仍有文本适配，内置 JSON 路径不再使用它。
文件大小限制在根字节入口检查，嵌套节点不作为独立文件重复检查大小。
API 以[反射参考](../reference/reflection.md)为准。

验证过程发现深层节点回调在 Windows Debug 下触发栈溢出，已将对象递归与其他类别分派分离，
减少每层携带的临时状态。测试确认 64 层正常编码/解码、65 层报告错误，未提高栈配置或降低限制。

Windows Clang Debug 全量构建、21 项相关测试通过。新增覆盖独立注册表、节点 codec 递归深度、
文件大小限制、重复格式头、严格未知字段、必填字段与嵌套错误路径；资源、场景和编辑器回归通过。
clang-format 检查、git diff --check 和核心分层检查通过。
未运行大场景、完整平台矩阵或远端 CI。未调整 value 注册归属及覆盖校验分层。
