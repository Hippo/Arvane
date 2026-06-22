#include "arv/client/gameplay_ui_model.hpp"

#include "arv/client/item_ui_model.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace arv::client {
namespace {

[[nodiscard]] std::optional<f32> passive_chat_opacity(
    const ChatHistoryEntry& entry,
    std::chrono::steady_clock::time_point now,
    const io::UiConfig::ChatConfig& config) noexcept {
  const f32 age_seconds = std::max(
      0.0F,
      std::chrono::duration<f32>(now - entry.received_at).count());
  if (age_seconds <= config.passive_visible_seconds) {
    return 1.0F;
  }
  if (config.passive_fade_seconds <= 0.0F) {
    return std::nullopt;
  }
  const f32 fade_progress =
      (age_seconds - config.passive_visible_seconds) / config.passive_fade_seconds;
  if (fade_progress >= 1.0F) {
    return std::nullopt;
  }
  return std::clamp(1.0F - fade_progress, 0.0F, 1.0F);
}

[[nodiscard]] ui::ChatLineView chat_line_view_from_entry(
    const ChatHistoryEntry& entry,
    f32 opacity) {
  ui::ChatLineView line;
  line.text = arv::text::to_plain_text(entry.message);
  line.opacity = opacity;
  line.runs = arv::text::flatten_rich_text(entry.message);
  return line;
}

[[nodiscard]] ui::HealthHudModel health_model_for_client(
    const ClientRuntime& client,
    Vec2f viewport) {
  ui::HealthHudModel health;
  health.viewport = viewport;
  if (const std::optional<EntityNetID> local_entity = client.local_entity_id();
      local_entity.has_value()) {
    const ReplicatedEntityMirror* mirror =
        client.find_replicated_entity(local_entity.value());
    if (mirror != nullptr && mirror->has_health) {
      health.visible = true;
      health.current = mirror->health_current;
      health.max = mirror->health_max;
      health.section_value = 1.0F;
    }
  }
  return health;
}

[[nodiscard]] std::string tab_id(PlayerMenuTab tab) {
  switch (tab) {
    case PlayerMenuTab::Inventory:
      return "inventory";
    case PlayerMenuTab::Crafting:
      return "crafting";
    case PlayerMenuTab::Settings:
      return "settings";
    case PlayerMenuTab::ExitGame:
      return "exit";
  }
  return "inventory";
}

[[nodiscard]] std::string settings_section_id(PlayerMenuSettingsSection section) {
  switch (section) {
    case PlayerMenuSettingsSection::Overview:
      return "overview";
    case PlayerMenuSettingsSection::Render:
      return "render";
    case PlayerMenuSettingsSection::KeyBinds:
      return "key_binds";
    case PlayerMenuSettingsSection::Controls:
      return "controls";
    case PlayerMenuSettingsSection::Interface:
      return "interface";
  }
  return "overview";
}

[[nodiscard]] std::vector<ui::PlayerMenuTabButtonView> player_menu_tabs(
    PlayerMenuTab active) {
  return {
      {"inventory", "Inventory", active == PlayerMenuTab::Inventory},
      {"crafting", "Crafting", active == PlayerMenuTab::Crafting},
      {"settings", "Settings", active == PlayerMenuTab::Settings},
      {"exit", "Exit Game", active == PlayerMenuTab::ExitGame},
  };
}

[[nodiscard]] bool contains_case_insensitive(
    std::string_view haystack,
    std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  std::string lower_haystack{haystack};
  std::string lower_needle{needle};
  std::transform(
      lower_haystack.begin(),
      lower_haystack.end(),
      lower_haystack.begin(),
      [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  std::transform(
      lower_needle.begin(),
      lower_needle.end(),
      lower_needle.begin(),
      [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return lower_haystack.find(lower_needle) != std::string::npos;
}

[[nodiscard]] ui::UiItemStackView stack_view_or_empty(
    player::ItemID item,
    u16 count,
    const player::ItemRegistry* item_registry,
    const asset::TextureAtlas* item_atlas,
    const asset::BlockTextureAtlas* block_atlas) {
  if (item_registry == nullptr || item_atlas == nullptr || block_atlas == nullptr) {
    return {};
  }
  std::optional<ui::UiItemStackView> view = item_stack_for_ui(
      std::optional<player::ItemStack>{player::ItemStack{item, count}},
      *item_registry,
      *item_atlas,
      *block_atlas);
  return view.value_or(ui::UiItemStackView{});
}

[[nodiscard]] std::string item_display_name(
    player::ItemID item_id,
    const player::ItemRegistry* item_registry,
    std::string_view fallback) {
  const player::ItemRecord* item =
      item_registry == nullptr ? nullptr : item_registry->item(item_id);
  if (item == nullptr || item->display_name.empty()) {
    return std::string{fallback};
  }
  return item->display_name;
}

[[nodiscard]] bool recipe_matches_search(
    const crafting::CraftingRecipe& recipe,
    std::string_view label,
    std::string_view search) {
  if (search.empty()) {
    return true;
  }
  if (contains_case_insensitive(recipe.id, search) ||
      contains_case_insensitive(label, search) ||
      contains_case_insensitive(recipe.result.item_name, search)) {
    return true;
  }
  for (const std::string& term : recipe.search_terms) {
    if (contains_case_insensitive(term, search)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] ui::CraftingUiModel build_crafting_ui_model(
    const ClientRuntime& client,
    const player::Inventory& inventory,
    const crafting::CraftingRecipeRegistry* registry,
    const player::ItemRegistry* item_registry,
    const asset::TextureAtlas* item_atlas,
    const asset::BlockTextureAtlas* block_atlas) {
  ui::CraftingUiModel model;
  model.search = client.player_menu_search();
  model.search_cursor = client.player_menu_search_cursor();
  model.search_selection_start = client.player_menu_search_selection_start();
  model.search_selection_end = client.player_menu_search_selection_end();
  model.search_active = true;
  if (registry == nullptr || item_registry == nullptr) {
    return model;
  }

  std::string selected_category = client.selected_crafting_category_id();
  if ((selected_category.empty() || !registry->has_category(selected_category)) &&
      !registry->categories().empty()) {
    selected_category = registry->categories().front().id;
  }

  for (const crafting::CraftingCategory& category : registry->categories()) {
    model.categories.push_back(
        {category.id,
         category.display_name.empty() ? category.id : category.display_name,
         stack_view_or_empty(category.icon.item, 1U, item_registry, item_atlas, block_atlas),
         category.id == selected_category});
  }

  const std::span<const std::string> unlocked = client.unlocked_recipe_ids();
  std::string selected_recipe_id = client.selected_crafting_recipe_id();
  std::vector<const crafting::CraftingRecipe*> visible_recipes;
  for (const crafting::CraftingRecipe& recipe : registry->recipes()) {
    if (!crafting::recipe_is_unlocked(unlocked, recipe.id)) {
      continue;
    }
    const std::string label =
        item_display_name(recipe.result.item, item_registry, recipe.id);
    const bool search_matches = recipe_matches_search(recipe, label, model.search);
    if (!model.search.empty()) {
      if (!search_matches) {
        continue;
      }
    } else if (recipe.category != selected_category) {
      continue;
    }
    visible_recipes.push_back(&recipe);
    if (selected_recipe_id.empty()) {
      selected_recipe_id = recipe.id;
    }
  }

  if (!selected_recipe_id.empty() &&
      std::find_if(
          visible_recipes.begin(),
          visible_recipes.end(),
          [&](const crafting::CraftingRecipe* recipe) {
            return recipe->id == selected_recipe_id;
          }) == visible_recipes.end()) {
    selected_recipe_id = visible_recipes.empty() ? std::string{} : visible_recipes.front()->id;
  }

  for (const crafting::CraftingRecipe* recipe : visible_recipes) {
    const std::vector<crafting::CraftingIngredientAvailability> availability =
        crafting::ingredient_availability(inventory, *recipe, *item_registry);
    const bool craftable = std::all_of(
        availability.begin(),
        availability.end(),
        [](const crafting::CraftingIngredientAvailability& item) {
          return item.satisfied;
        });
    model.recipes.push_back(
        {recipe->id,
         item_display_name(recipe->result.item, item_registry, recipe->id),
         stack_view_or_empty(recipe->icon.item, recipe->result.count, item_registry, item_atlas, block_atlas),
         craftable,
         recipe->id == selected_recipe_id});
  }

  const crafting::CraftingRecipe* selected =
      selected_recipe_id.empty() ? nullptr : registry->recipe(selected_recipe_id);
  if (selected != nullptr) {
    model.has_selected_recipe = true;
    model.selected_recipe_id = selected->id;
    model.selected_recipe_label =
        item_display_name(selected->result.item, item_registry, selected->id);
    model.selected_recipe_icon =
        stack_view_or_empty(selected->result.item, selected->result.count, item_registry, item_atlas, block_atlas);
    const std::vector<crafting::CraftingIngredientAvailability> availability =
        crafting::ingredient_availability(inventory, *selected, *item_registry);
    model.selected_recipe_craftable = std::all_of(
        availability.begin(),
        availability.end(),
        [](const crafting::CraftingIngredientAvailability& item) {
          return item.satisfied;
        });
    for (const crafting::CraftingIngredientAvailability& ingredient : availability) {
      model.selected_ingredients.push_back(
          {ingredient.label, ingredient.required, ingredient.owned, ingredient.satisfied});
    }
  }
  return model;
}

} // namespace

usize gameplay_chat_history_max_scroll_offset(
    usize history_size,
    usize max_visible_messages) noexcept {
  const usize visible = std::max<usize>(1U, max_visible_messages);
  return history_size > visible ? history_size - visible : 0U;
}

usize gameplay_chat_scroll_steps(f32 wheel_y) noexcept {
  if (wheel_y == 0.0F || !std::isfinite(wheel_y)) {
    return 0U;
  }
  return static_cast<usize>(std::max(1.0F, std::ceil(std::abs(wheel_y))));
}

void refresh_gameplay_chat_lines(
    std::vector<ui::ChatLineView>& chat_lines,
    const ClientRuntime& client,
    const io::UiConfig::ChatConfig& chat_config,
    std::chrono::steady_clock::time_point now,
    usize& history_scroll_offset) {
  chat_lines.clear();
  const std::span<const ChatHistoryEntry> chat_history = client.chat_history();
  if (client.chat_open()) {
    const usize max_lines = std::max<usize>(1U, chat_config.open_max_lines);
    history_scroll_offset = std::min(
        history_scroll_offset,
        gameplay_chat_history_max_scroll_offset(chat_history.size(), max_lines));
    chat_lines.reserve(chat_history.size());
    for (usize index = 0U; index < chat_history.size(); ++index) {
      chat_lines.emplace_back(chat_line_view_from_entry(chat_history[index], 1.0F));
    }
    return;
  }

  history_scroll_offset = 0U;
  if (chat_config.passive_max_lines == 0U) {
    return;
  }
  for (usize reverse_index = chat_history.size(); reverse_index > 0U; --reverse_index) {
    const ChatHistoryEntry& entry = chat_history[reverse_index - 1U];
    const std::optional<f32> opacity =
        passive_chat_opacity(entry, now, chat_config);
    if (!opacity.has_value()) {
      continue;
    }
    chat_lines.emplace_back(chat_line_view_from_entry(entry, opacity.value()));
    if (chat_lines.size() >= chat_config.passive_max_lines) {
      break;
    }
  }
  std::reverse(chat_lines.begin(), chat_lines.end());
}

ui::RidePromptModel build_ride_prompt_model(RidePromptModelInput input) {
  ui::RidePromptModel model;
  model.viewport = input.viewport;
  if (!input.interactions_available ||
      input.gameplay_blocked ||
      input.inventory_open ||
      input.chat_open ||
      input.key_label.empty()) {
    return model;
  }
  if (input.local_player_mounted) {
    model.visible = input.mount_exit_prompt_alpha > 0.0F;
    model.key_label = std::string{input.key_label};
    model.suffix = " to Exit";
    model.alpha = std::clamp(input.mount_exit_prompt_alpha, 0.0F, 1.0F);
    return model;
  }
  if (!input.has_targeted_rideable || !input.has_targeted_rideable()) {
    return model;
  }
  model.visible = true;
  model.key_label = std::string{input.key_label};
  model.suffix = " to Ride";
  model.alpha = 1.0F;
  return model;
}

ui::GameplayUiModel build_gameplay_ui_model(GameplayUiModelInput input) {
  const bool chat_open = input.client.chat_open();
  ui::ChatUiModel chat_model{
      true,
      chat_open,
      input.chat_lines,
      input.client.chat_draft(),
      input.client.chat_draft_cursor(),
      input.client.chat_draft_selection_start(),
      input.client.chat_draft_selection_end(),
      input.client.pending_chat_url_confirmation(),
      chat_open ? input.chat_config.open_max_lines : input.chat_config.passive_max_lines,
      input.language,
  };
  chat_model.history_scroll_offset =
      chat_open ? input.chat_history_scroll_offset : 0U;

  const usize hotbar_slot_count =
      std::min<usize>(input.inventory_slots.size(), player::DefaultHotbarSlotCount);

  const bool container_open = input.client.container_open() &&
                              input.container_layout != nullptr;
  const bool player_menu_open = input.client.inventory_open() && !container_open;
  ui::CrosshairHudModel crosshair = std::move(input.crosshair);
  crosshair.viewport = input.viewport;
  crosshair.visible = crosshair.visible && !input.client.inventory_open() &&
                      !chat_open && !container_open;

  ui::InventoryUiModel inventory_model{
      player_menu_open && input.client.player_menu_tab() == PlayerMenuTab::Inventory,
      std::string{input.inventory_title},
      input.client.selected_hotbar_slot(),
      player::DefaultHotbarSlotCount,
      input.viewport,
      input.inventory_slots,
      input.carried_inventory_stack,
      input.cursor_position,
  };
  ui::PlayerMenuUiModel player_menu;
  player_menu.visible = player_menu_open;
  player_menu.title = std::string{input.player_menu_title};
  player_menu.active_tab_id = tab_id(input.client.player_menu_tab());
  player_menu.viewport = input.viewport;
  player_menu.cursor_position = input.cursor_position;
  player_menu.tabs = player_menu_tabs(input.client.player_menu_tab());
  player_menu.inventory = inventory_model;
  player_menu.crafting = build_crafting_ui_model(
      input.client,
      input.client.inventory(),
      input.crafting_registry,
      input.item_registry,
      input.item_atlas,
      input.block_atlas);
  player_menu.settings.active_section_id =
      settings_section_id(input.client.player_menu_settings_section());
  player_menu.settings.scroll_y = input.client.player_menu_settings_scroll_y();
  player_menu.settings.render = input.render_settings;
  player_menu.settings.controls = input.controls_settings;
  player_menu.settings.interface = input.interface_settings;
  player_menu.settings.key_binds.assign(
      input.key_bind_entries.begin(),
      input.key_bind_entries.end());
  if (const std::optional<PlayerMenuKeyBindCapture>& capture =
          input.client.player_menu_key_bind_capture();
      capture.has_value()) {
    player_menu.settings.capture =
        ui::PlayerMenuSettingsCaptureView{capture->action_name, capture->slot};
  }
  player_menu.show_quit = input.show_quit;

  return {
      {
          input.client.selected_hotbar_slot(),
          player::DefaultHotbarSlotCount,
          input.viewport,
          std::span<const ui::UiItemStackView>{
              input.inventory_slots.data(),
              hotbar_slot_count},
          std::string{input.hotbar_item_popup_name},
          input.hotbar_item_popup_alpha,
          !input.client.inventory_open() && !container_open,
      },
      {},
      std::move(player_menu),
      std::move(chat_model),
      {
          input.client.tab_menu_visible(),
          std::string{input.tab_title},
          "",
          input.tab_players,
      },
      {},
      {input.debug_fps_visible, input.displayed_fps},
      health_model_for_client(input.client, input.viewport),
      std::move(input.ride_prompt),
      {
          container_open,
          std::string{input.container_title},
          input.container_layout,
          input.client.selected_hotbar_slot(),
          player::DefaultHotbarSlotCount,
          input.viewport,
          input.container_slots,
          input.inventory_slots,
          input.carried_inventory_stack,
          input.cursor_position,
      },
      std::move(crosshair),
  };
}

ui::GameplayUiModel build_gameplay_ui_model_from_snapshots(
    GameplayUiSnapshotModelInput input) {
  const player::ContainerUiLayoutDefinition* container_layout =
      input.client.container_open() && input.container_layouts != nullptr
          ? input.container_layouts->find(input.client.active_container_layout())
          : nullptr;

  std::string inventory_title;
  if (!input.inventory_title.empty()) {
    inventory_title = std::move(input.inventory_title);
  } else if (input.language != nullptr) {
    inventory_title = input.language->text("ui.inventory.title", "Inventory");
  } else {
    inventory_title = "Inventory";
  }

  std::string player_menu_title;
  if (!input.player_menu_title.empty()) {
    player_menu_title = std::move(input.player_menu_title);
  } else {
    player_menu_title = inventory_title;
  }

  std::string tab_title;
  if (!input.tab_title.empty()) {
    tab_title = std::move(input.tab_title);
  } else if (input.language != nullptr) {
    tab_title = input.language->text("ui.tab.players", "Players");
  } else {
    tab_title = "Players";
  }

  input.ride_prompt.viewport = input.viewport;
  const std::string_view hotbar_item_popup_name =
      input.hotbar_item_popup_alpha > 0.0F
          ? input.hotbar_item_popup_name
          : std::string_view{};

  return build_gameplay_ui_model(
      {
          .client = input.client,
          .chat_config = input.chat_config,
          .inventory_slots = input.inventory_slots,
          .container_slots = input.container_slots,
          .carried_inventory_stack = std::move(input.carried_inventory_stack),
          .chat_lines = input.chat_lines,
          .chat_history_scroll_offset = input.chat_history_scroll_offset,
          .tab_players = input.tab_players,
          .inventory_title = inventory_title,
          .player_menu_title = player_menu_title,
          .container_title = input.client.active_container_title(),
          .container_layout = container_layout,
          .crafting_registry = input.crafting_registry,
          .item_registry = input.item_registry,
          .item_atlas = input.item_atlas,
          .block_atlas = input.block_atlas,
          .render_settings = input.render_settings,
          .controls_settings = input.controls_settings,
          .interface_settings = input.interface_settings,
          .key_bind_entries = input.key_bind_entries,
          .show_quit = input.show_quit,
          .tab_title = tab_title,
          .viewport = input.viewport,
          .cursor_position = input.cursor_position,
          .displayed_fps = input.displayed_fps,
          .hotbar_item_popup_name = hotbar_item_popup_name,
          .hotbar_item_popup_alpha = input.hotbar_item_popup_alpha,
          .crosshair = std::move(input.crosshair),
          .ride_prompt = std::move(input.ride_prompt),
          .language = input.language,
          .debug_fps_visible = input.debug_fps_visible,
      });
}

} // namespace arv::client
