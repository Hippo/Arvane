#include "arv_mason/mason_plugin.hpp"

#include "../string_utils.hpp"
#include "command_utils.hpp"

#include "arv_mason/ids.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv_mason {

std::vector<std::string> MasonPlugin::load_blueprint_index(ArvWorldHandle world) const {
  std::vector<std::string> names;
  auto loaded = plugin_.persistent_data().world(world).load(ids::blueprint_index);
  if (!loaded) {
    return names;
  }
  auto text = loaded.value.string_value("names");
  if (!text) {
    return names;
  }
  std::string_view value = text.value;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find('\n', start);
    std::string_view name = end == std::string_view::npos
                                ? value.substr(start)
                                : value.substr(start, end - start);
    if (!name.empty() && valid_blueprint_name(name)) {
      names.emplace_back(name);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1U;
  }
  return names;
}

void MasonPlugin::save_blueprint_index(ArvWorldHandle world, const std::vector<std::string>& names) const {
  auto document = plugin_.data().create_document(1);
  if (!document) {
    return;
  }
  std::string text;
  for (const std::string& name : names) {
    text.append(name);
    text.push_back('\n');
  }
  (void)document.value.set_string("names", text);
  (void)plugin_.persistent_data().world(world).save(ids::blueprint_index, document.value);
}

bool MasonPlugin::save_blueprint_document(
    ArvWorldHandle world,
    std::string_view name,
    std::string_view abp_text) const {
  auto document = plugin_.data().create_document(1);
  if (!document) {
    return false;
  }
  (void)document.value.set_string("format", blueprint_format_id);
  (void)document.value.set_string("name", name);
  (void)document.value.set_string("abp", abp_text);
  return static_cast<bool>(plugin_.persistent_data().world(world).save(
      blueprint_key(name),
      document.value));
}

std::optional<std::string> MasonPlugin::load_blueprint_document(
    ArvWorldHandle world,
    std::string_view name) const {
  auto loaded = plugin_.persistent_data().world(world).load(blueprint_key(name));
  if (!loaded) {
    return std::nullopt;
  }
  auto text = loaded.value.string_value("abp");
  if (!text) {
    return std::nullopt;
  }
  return std::string{text.value};
}

std::optional<MasonClipboard> MasonPlugin::clipboard_from_blueprint(
    arv::CommandContext& ctx,
    const Blueprint& blueprint) const {
  MasonClipboard clipboard;
  clipboard.size = blueprint.size;
  clipboard.source_min = {0, 0, 0};
  clipboard.origin = blueprint.origin;
  clipboard.blocks.reserve(blueprint.blocks.size());

  std::vector<std::uint32_t> runtime_palette;
  runtime_palette.reserve(blueprint.palette.size());
  for (const std::string& stable_id : blueprint.palette) {
    auto resolved = plugin_.registries().find_block_variant(stable_id);
    if (!resolved) {
      ctx.error("Blueprint references an unknown block state: " + stable_id + ".");
      return std::nullopt;
    }
    runtime_palette.push_back(resolved.value);
  }
  for (const std::uint32_t state : blueprint.blocks) {
    if (state >= runtime_palette.size()) {
      ctx.error("Blueprint block references an invalid palette index.");
      return std::nullopt;
    }
    clipboard.blocks.push_back({runtime_palette[state], blueprint.palette[state]});
  }
  return clipboard;
}

arv::Result MasonPlugin::handle_blueprint(
    arv::CommandContext& ctx,
    const Args& args,
    bool confirmed) {
  if (args.empty()) {
    ctx.error("Usage: /mason blueprint <save|load|paste|list|info>.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }
  const std::string_view action = args[0];
  const std::optional<ArvWorldHandle> world = require_world(ctx);
  if (!world.has_value()) {
    ctx.error("Blueprint commands require a player world.");
    return arv::Result{arv::ResultCode::InvalidState, "world required"};
  }

  if (action == "list") {
    if (!check_custom_permission(ctx, ids::perm_blueprint_list)) {
      return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
    }
    const std::vector<std::string> names = load_blueprint_index(*world);
    if (names.empty()) {
      ctx.info("No blueprints saved.");
      return arv::Result::ok();
    }
    std::ostringstream out;
    out << "Blueprints:";
    for (const std::string& name : names) {
      out << ' ' << name;
    }
    ctx.info(out.str());
    return arv::Result::ok();
  }

  if (args.size() < 2U) {
    ctx.error("Blueprint name is required.");
    return arv::Result{arv::ResultCode::InvalidArgument, "blueprint name required"};
  }
  const std::string name{args[1]};
  if (!valid_blueprint_name(name)) {
    ctx.error("Invalid blueprint name. Use only letters, numbers, underscore, dash, and dot.");
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid blueprint name"};
  }

  if (action == "save") {
    if (!check_custom_permission(ctx, ids::perm_blueprint_save)) {
      return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
    }
    MasonPlayerState* state = state_for(ctx);
    if (state == nullptr) {
      return arv::Result{arv::ResultCode::InvalidState, "player required"};
    }
    SelectionBoundsResult bounds = normalized_bounds(state->selection);
    if (!bounds.ok) {
      ctx.error(bounds.message);
      return arv::Result{arv::ResultCode::InvalidState, "selection incomplete"};
    }
    if (!has_limit_bypass(ctx) && bounds.bounds.block_count > config_.limits.max_blueprint_blocks) {
      ctx.error(
          "Blueprint exceeds limit: " + count_text(bounds.bounds.block_count) +
          " blocks. Limit: " + count_text(config_.limits.max_blueprint_blocks) + ".");
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint limit exceeded"};
    }
    const std::optional<ArvBlockPos> origin = require_block_position(ctx);
    if (!origin.has_value()) {
      ctx.error("Blueprint save requires a player position.");
      return arv::Result{arv::ResultCode::InvalidState, "position required"};
    }
    auto clipboard = copy_selection_to_clipboard(ctx, bounds.bounds, *origin);
    if (!clipboard.has_value()) {
      return arv::Result{arv::ResultCode::InternalError, "copy failed"};
    }
    Blueprint blueprint = blueprint_from_clipboard(name, *clipboard);
    BlueprintCodecResult encoded = encode_blueprint(blueprint);
    if (!encoded.ok) {
      ctx.error(encoded.message);
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint encode failed"};
    }
    if (!save_blueprint_document(*world, name, encoded.text)) {
      ctx.error("Failed to save blueprint.");
      return arv::Result{arv::ResultCode::InternalError, "blueprint save failed"};
    }
    std::vector<std::string> names = load_blueprint_index(*world);
    if (append_unique_string(names, name)) {
      std::sort(names.begin(), names.end());
      save_blueprint_index(*world, names);
    }
    ctx.success("Blueprint saved: " + name + ".abp.");
    return arv::Result::ok();
  }

  if (action == "load" || action == "info" || action == "paste") {
    const std::string permission = action == "load"
                                       ? std::string{ids::perm_blueprint_load}
                                   : action == "info"
                                       ? std::string{ids::perm_blueprint_info}
                                       : std::string{ids::perm_blueprint_paste};
    if (!check_custom_permission(ctx, permission)) {
      return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
    }
    auto document = load_blueprint_document(*world, name);
    if (!document.has_value()) {
      ctx.error("Blueprint not found: " + name + ".abp.");
      return arv::Result{arv::ResultCode::NotFound, "blueprint not found"};
    }
    BlueprintCodecResult decoded = decode_blueprint(*document);
    if (!decoded.ok) {
      ctx.error("Blueprint is corrupt: " + decoded.message);
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint corrupt"};
    }
    const std::uint64_t blocks = blueprint_volume(decoded.blueprint.size);
    if (!has_limit_bypass(ctx) && blocks > config_.limits.max_blueprint_blocks) {
      ctx.error(
          "Blueprint exceeds limit: " + count_text(blocks) +
          " blocks. Limit: " + count_text(config_.limits.max_blueprint_blocks) + ".");
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint limit exceeded"};
    }
    if (action == "info") {
      std::ostringstream out;
      out << "Blueprint " << name << ".abp: "
          << decoded.blueprint.size.x << " x " << decoded.blueprint.size.y
          << " x " << decoded.blueprint.size.z << " = " << blocks
          << " blocks, palette " << decoded.blueprint.palette.size() << ".";
      ctx.info(out.str());
      return arv::Result::ok();
    }
    auto clipboard = clipboard_from_blueprint(ctx, decoded.blueprint);
    if (!clipboard.has_value()) {
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint resolve failed"};
    }
    MasonPlayerState* state = state_for(ctx);
    if (state == nullptr) {
      return arv::Result{arv::ResultCode::InvalidState, "player required"};
    }
    state->clipboard = std::move(*clipboard);
    if (action == "load") {
      ctx.success("Blueprint loaded: " + name + ".abp.");
      return arv::Result::ok();
    }
    Args paste_args;
    for (std::size_t i = 2U; i < args.size(); ++i) {
      paste_args.push_back(args[i]);
    }
    if (!require_limit_or_confirmation(
            ctx,
            *state,
            {PendingOperationKind::BlueprintPaste, copy_args(args)},
            blocks,
            config_.limits.max_blueprint_blocks,
            confirmed)) {
      return arv::Result::ok();
    }
    return handle_paste(ctx, paste_args, true);
  }

  ctx.error("Usage: /mason blueprint <save|load|paste|list|info>.");
  return arv::Result{arv::ResultCode::InvalidArgument, "unknown blueprint action"};
}

} // namespace arv_mason
