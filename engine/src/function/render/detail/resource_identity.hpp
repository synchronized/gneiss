// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/function/render/render_resource_data.hpp>
#include <atomic>

namespace gneiss::render::detail {
/** 资源身份原子发布不可变内容；读取者持有版本，不借用发布中的可变记录。
 * accept/read 是所属线程的已接受版本；consume/read_consumed 是队列消费版本。
 * publish 仅用于尚未暴露的新身份或同步消费；跨资源顺序由 Render 执行器保证。 */
template <typename Data> class resource_identity {
public:
  void publish(std::shared_ptr<const Data> data) noexcept {
    accept(data);
    consume(std::move(data));
  }
  void accept(std::shared_ptr<const Data> data) noexcept {
    data_.store(std::move(data), std::memory_order_release);
  }
  void consume(std::shared_ptr<const Data> data) noexcept {
    consumed_.store(std::move(data), std::memory_order_release);
  }
  std::shared_ptr<const Data> read_consumed() const noexcept {
    return consumed_.load(std::memory_order_acquire);
  }
  std::shared_ptr<const Data> read() const noexcept {
    return data_.load(std::memory_order_acquire);
  }

private:
  std::atomic<std::shared_ptr<const Data>> data_;
  std::atomic<std::shared_ptr<const Data>> consumed_;
};
/** 材质内容与纹理槽位绑定属于同一版本，必须一起发布。 */
template <> class resource_identity<material_data> {
public:
  using dependencies = std::array<std::shared_ptr<const resource_identity<texture_data>>, 5>;
  struct version {
    std::shared_ptr<const material_data> data;
    dependencies textures{};
  };
  void publish(std::shared_ptr<const material_data> data, dependencies textures = {}) {
    auto next = std::make_shared<const version>(version{std::move(data), std::move(textures)});
    accept(next);
    consume(std::move(next));
  }
  void accept(std::shared_ptr<const version> next) noexcept {
    version_.store(std::move(next), std::memory_order_release);
  }
  void consume(std::shared_ptr<const version> next) noexcept {
    consumed_.store(std::move(next), std::memory_order_release);
  }
  std::shared_ptr<const version> snapshot_consumed() const noexcept {
    return consumed_.load(std::memory_order_acquire);
  }
  std::shared_ptr<const version> snapshot() const noexcept {
    return version_.load(std::memory_order_acquire);
  }
  std::shared_ptr<const material_data> read() const noexcept {
    auto current = snapshot();
    return current ? current->data : nullptr;
  }

private:
  std::atomic<std::shared_ptr<const version>> version_;
  std::atomic<std::shared_ptr<const version>> consumed_;
};
/** 某次捕获固定的内容；后续发布不会修改这些数据。纹理槽位与材质一致。 */
struct scene_resource_versions {
  std::shared_ptr<const mesh_data> mesh;
  std::shared_ptr<const material_data> material;
  std::array<std::shared_ptr<const texture_data>, 5> textures{};
};
struct scene_resource_pin {
  std::shared_ptr<const resource_identity<mesh_data>> mesh;
  std::shared_ptr<const resource_identity<material_data>> material;

  /** 捕获各身份当前版本；材质槽位一致，纹理内容允许独立更新。 */
  scene_resource_versions capture() const {
    scene_resource_versions versions;
    versions.mesh = mesh ? mesh->read_consumed() : nullptr;
    const auto current = material ? material->snapshot_consumed() : nullptr;
    if (current) {
      versions.material = current->data;
      for (std::size_t index = 0; index < versions.textures.size(); ++index)
        if (current->textures[index])
          versions.textures[index] = current->textures[index]->read_consumed();
    }
    return versions;
  }
};
}
