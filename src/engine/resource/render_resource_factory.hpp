// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/texture/texture_resource.hpp"
#include "engine/resource/texture/texture_view.hpp"
namespace gneiss::engine {
class render_service;
/** 所属线程取得 RID 的唯一资源引用；无效 RID 返回空，分配失败抛出 bad_alloc。 */
[[nodiscard]] GNEISS_API mesh reference_mesh(render_service& render, rid handle);
/** 所属线程取得 RID 的唯一资源引用；无效 RID 返回空，分配失败抛出 bad_alloc。 */
[[nodiscard]] GNEISS_API texture reference_texture(render_service& render, rid handle);
/** 所属线程取得 RID 的唯一资源引用；无效 RID 返回空，分配失败抛出 bad_alloc。 */
[[nodiscard]] GNEISS_API material reference_material(render_service& render, rid handle);
/** 程序化纹理入口；复制像素，成功后由强引用负责销毁。 */
[[nodiscard]] GNEISS_API result create_texture(render_service& render, const texture_view& desc,
                                               texture& output) noexcept;
} // namespace gneiss::engine
