// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ACTION_MAP_HPP_
#define GNEISS_ASSET_ACTION_MAP_HPP_

#include "engine/core/guid.hpp"
#include "engine/core/serialization/diagnostic.hpp"
#include "engine/resource/asset.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine::serialization {
struct diagnostic;
}

namespace gneiss::engine {

struct action_binding final {
  inline static constexpr std::string_view type_name = "action_binding";
  inline static constexpr guid type_guid = guid::literal("f369fe72-b420-4ecb-abfb-c5d58df57b7b");
  std::uint32_t physical_key{};
  float scale{};
};

struct action_definition final {
  inline static constexpr std::string_view type_name = "action_definition";
  inline static constexpr guid type_guid = guid::literal("88ef4852-3bfc-4ee3-8aa6-6e752bd056a4");
  std::string name;
  std::vector<action_binding> bindings;
};

/** 输入文件及反射解码统一上限。 */
inline constexpr std::size_t maximum_action_map_bytes = 1024U * 1024U;

/** 可共享的只读描述；实时输入状态由 InputService 单独管理。 */
struct action_map_description final {
  inline static constexpr std::string_view type_name = "action_map_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::literal("2c8319d2-673f-4a51-9380-7ac502e694b1")};
  inline static constexpr std::size_t maximum_source_bytes = maximum_action_map_bytes;
  std::vector<action_definition> actions;
};

namespace detail {
struct action_map_loader;
}
/** 共享输入映射资产；输入服务采用独立描述，不跟随重载自动改变。 */
class action_map final : public asset {
public:
  /** 所属线程取得快照；既有快照不随重载改变。 */
  [[nodiscard]] ref<const action_map_description> snapshot() const noexcept { return content_; }

private:
  friend struct detail::action_map_loader;
  action_map(asset_identity source, ref<const action_map_description> content)
      : asset(runtime_type_of<action_map>()), content_(std::move(content)) {
    identity_ = std::move(source);
  }
  mutable ref<const action_map_description> content_;
};

/** 原生构造与 JSON 加载共享语义约束；不改变输入映射。 */
[[nodiscard]] serialization::diagnostic
validate_action_map(const action_map_description& map) noexcept;

} // namespace gneiss::engine

#endif
