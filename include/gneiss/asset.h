// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_H_
#define GNEISS_ASSET_H_

#include <stdint.h>

#include <gneiss/core/export.h>
#include <gneiss/core/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 校验规范形式的 UTF-8 资产 URI。
 *
 * 首版只接受 `asset://目录/文件`。路径必须使用正斜杠，不能为空，不允许空段、`.`、`..`、
 * 控制字符、反斜杠、冒号、查询、片段或百分号编码。函数不访问文件系统，且线程安全。
 */
GNEISS_API gneiss_result gneiss_asset_uri_validate(const char* uri, uint64_t uri_length);

#ifdef __cplusplus
}
#endif

#endif
