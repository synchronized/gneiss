// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <caors/assets.h>
#include <caors/tasks.h>
#include <cstdio>
#include <stdexcept>

namespace {
struct document final : caors::resources::resource {
  int revision{};
};
class controlled_loader final : public caors::resources::loader<document> {
public:
  caors::resources::load_completion<document> pending;
  caors::resource_guid pending_id;
  int revision = 2;
  caors::resources::load_result<document> load(caors::resource_guid id,
                                               caors::memory_resource* = nullptr) override {
    caors::memory::unique_ptr<document> value{new document};
    value->set_guid(id);
    value->revision = revision;
    return {{}, std::move(value)};
  }
  caors::error start(caors::resources::async_load_context context,
                     caors::resources::load_completion<document> completion) override {
    pending_id = context.guid;
    pending = std::move(completion);
    return {};
  }
};
void require(bool ok) {
  if (!ok)
    throw std::runtime_error("publication probe setup failed");
}
void probe(bool existing, bool cancel_one = false) {
  caors::tasks::execution_context worker, completions;
  controlled_loader loader;
  caors::resources::cache<document> cache;
  caors::assets::asset_manager manager({worker.get_executor(), completions.get_executor()});
  require(!manager.register_type(cache, loader, caors::string_name{"publication.document"}));
  caors::guid::byte_array bytes{};
  bytes[0] = 1;
  const caors::resource_guid id{caors::guid::from_bytes(bytes)};
  if (existing) {
    auto initial = caors::resources::make_handle<document>(nullptr);
    require(static_cast<bool>(initial));
    initial->set_guid(id);
    require(cache.insert(initial));
  }
  auto older = existing ? manager.request_reload<document>(id) : manager.request_load<document>(id);
  caors::resources::load_request<document> independent;
  if (cancel_one) {
    independent = manager.request_reload<document>(id);
    require(older.cancel());
  }
  (void)worker.drain();
  require(cancel_one ? !independent.done() : !older.done());
  auto newer = existing ? manager.database().reload_handle<document>(id)
                        : manager.database().load_handle<document>(id);
  require(!newer.status && newer.value && newer.value->revision == 2);
  require(cache.find_handle(id)->revision == 2);
  loader.revision = 1;
  require(loader.pending.complete(loader.load(loader.pending_id)));
  (void)completions.drain();
  require(older.done());
  const auto current = cache.find_handle(id);
  require(static_cast<bool>(current));
  // 诊断程序只报告上游行为，不把旧覆盖语义写成产品验收要求。
  std::printf("%s: sync=2, after_old_async=%d, sync_handle=%d, stale_overwrite=%s\n",
              cancel_one ? "reload-cancel-one" : (existing ? "reload" : "first-load"),
              current->revision, newer.value->revision, current->revision == 1 ? "yes" : "no");
}
}
int main() {
  probe(false);
  probe(true);
  probe(true, true);
}
