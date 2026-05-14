#include "plugin_manager_internal.hpp"

namespace arv::plugin {

PluginResult PluginManager::load_plugins(PluginLoadOptions options) {
  if (options.packs == nullptr) {
    return PluginResult::failure({"", "plugin loading", {}, "pack manager is null"});
  }

  shutdown();
  impl_ = std::make_unique<PluginManagerImpl>();
  impl_->options = std::move(options);
  impl_->registration_open = true;

  const std::string platform_key = current_native_platform_key();
  const std::span<const asset::PackSource> stack = impl_->options.packs->stack();
  impl_->plugins.reserve(stack.size());

  for (usize pack_index = 0; pack_index < stack.size(); ++pack_index) {
    const asset::PackSource& pack = stack[pack_index];
    if (!pack.manifest.native.has_value()) {
      continue;
    }

    PluginRecord record;
    record.pack = &pack;
    record.pack_order = pack_index;
    record.info.pack_id = pack.manifest.id;
    record.info.pack_version = pack.manifest.version;
    record.info.namespaces = pack.manifest.namespaces;
    record.info.side = pack.manifest.side;
    record.info.entrypoint = pack.manifest.native->entrypoint;
    record.info.state = PluginState::Discovered;

    if (!impl_->options.policy.enabled) {
      record.info.state = PluginState::SkippedByPolicy;
      record.info.diagnostics.push_back("native plugins are disabled by runtime policy");
      append_diagnostic(
          *impl_,
          record.info.pack_id,
          "native plugin policy",
          pack.root / "pack.yaml",
          "skipped native plugin because native_plugins.enabled is false");
      impl_->plugins.push_back(std::move(record));
      continue;
    }
    if (!impl_->options.policy.allows(pack.manifest.id)) {
      record.info.state = PluginState::SkippedByPolicy;
      record.info.diagnostics.push_back("pack is not allowlisted for native plugin loading");
      append_diagnostic(
          *impl_,
          record.info.pack_id,
          "native plugin policy",
          pack.root / "pack.yaml",
          "skipped native plugin because pack is not in native_plugins.allowed_packs");
      impl_->plugins.push_back(std::move(record));
      continue;
    }
    if (pack.manifest.native->api_version != PluginApiVersion) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back("unsupported native plugin API version");
      PluginError error{
          pack.manifest.id,
          "plugin ABI validation",
          pack.root / "pack.yaml",
          "unsupported native plugin API version " +
              std::to_string(pack.manifest.native->api_version) +
              "; engine supports " + std::to_string(PluginApiVersion)};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    std::string path_error;
    std::optional<std::filesystem::path> library_path =
        detail::resolve_native_library_path(pack, platform_key, path_error);
    if (!library_path.has_value()) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back(path_error);
      PluginError error{
          pack.manifest.id,
          "native library selection",
          pack.root / "pack.yaml",
          path_error};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }
    record.info.library_path = library_path.value();
    if (!std::filesystem::is_regular_file(record.info.library_path)) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back("native library file does not exist");
      PluginError error{
          pack.manifest.id,
          "native library load",
          record.info.library_path,
          "native library file does not exist"};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    auto library = detail::DynamicLibrary::load(
        record.info.library_path,
        record.info.pack_id);
    if (!library) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back(library.error().message);
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(library.error());
      return PluginResult::failure(library.error());
    }
    record.library = std::move(library.value());
    record.info.state = PluginState::Loaded;

    const usize plugin_index = impl_->plugins.size();
    record.call_context = std::make_unique<PluginCallContext>();
    record.call_context->impl = impl_.get();
    record.call_context->plugin_index = plugin_index;
    record.api_storage = std::make_unique<PluginAPIStorage>(record.call_context.get());

    std::string symbol_error;
    void* raw_entrypoint = record.library.symbol(record.info.entrypoint, symbol_error);
    if (raw_entrypoint == nullptr) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back(symbol_error);
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          record.info.library_path,
          "entrypoint '" + record.info.entrypoint + "' was not found: " + symbol_error};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    impl_->plugins.push_back(std::move(record));
    auto entrypoint = reinterpret_cast<ArvEntrypoint>(raw_entrypoint);
    ThreadContextScope sync_scope{PluginThreadContext::Sync};
    bool ok = false;
    try {
      ok = entrypoint(&impl_->plugins[plugin_index].api_storage->api);
    } catch (const std::exception& ex) {
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          impl_->plugins[plugin_index].info.library_path,
          "entrypoint threw an exception: " + std::string{ex.what()}};
      impl_->plugins[plugin_index].info.state = PluginState::Failed;
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    } catch (...) {
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          impl_->plugins[plugin_index].info.library_path,
          "entrypoint threw an unknown exception"};
      impl_->plugins[plugin_index].info.state = PluginState::Failed;
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }
    if (!ok) {
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          impl_->plugins[plugin_index].info.library_path,
          "entrypoint returned false"};
      impl_->plugins[plugin_index].info.state = PluginState::Failed;
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    impl_->plugins[plugin_index].info.state = PluginState::Initialized;
    core::info(
        "plugin",
        "loaded native plugin pack={} library={}",
        impl_->plugins[plugin_index].info.pack_id,
        impl_->plugins[plugin_index].info.library_path.generic_string());
  }

  impl_->registration_open = false;
  return PluginResult::success();
}


} // namespace arv::plugin
