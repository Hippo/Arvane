#include "arv/client/client_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

[[nodiscard]] const arv::ui::KeyBindEntryView* find_entry(
    const std::vector<arv::ui::KeyBindEntryView>& entries,
    std::string_view action_name) {
  const auto found = std::find_if(
      entries.begin(),
      entries.end(),
      [action_name](const arv::ui::KeyBindEntryView& entry) {
        return entry.action_name == action_name;
      });
  return found == entries.end() ? nullptr : &*found;
}

TEST(ClientSettingsTests, KeyBindEntriesExposeLabelsAndConflicts) {
  arv::platform::InputBindings bindings = arv::platform::default_input_bindings();
  const arv::platform::InputBinding duplicate{
      arv::platform::InputDeviceType::Keyboard,
      "E",
      0U};
  arv::platform::bindings_for_action(
      bindings,
      arv::platform::LogicalAction::MoveForward) = {duplicate};
  arv::platform::bindings_for_action(
      bindings,
      arv::platform::LogicalAction::OpenInventory) = {duplicate};

  const std::vector<arv::ui::KeyBindEntryView> entries =
      arv::client::key_bind_entries_from_input_bindings(bindings);

  ASSERT_EQ(
      entries.size(),
      static_cast<arv::usize>(arv::platform::LogicalAction::Count));
  const arv::ui::KeyBindEntryView* forward = find_entry(entries, "MoveForward");
  ASSERT_NE(forward, nullptr);
  EXPECT_EQ(forward->display_name, "Move Forward");
  ASSERT_TRUE(forward->primary.has_binding);
  EXPECT_TRUE(forward->primary.conflict);
  EXPECT_EQ(forward->primary.binding.device, arv::ui::InputBindingDevice::Keyboard);
  EXPECT_EQ(forward->primary.binding.input_code, "E");
  EXPECT_FALSE(forward->primary.binding.label.empty());

  const arv::ui::KeyBindEntryView* inventory = find_entry(entries, "OpenInventory");
  ASSERT_NE(inventory, nullptr);
  ASSERT_TRUE(inventory->primary.has_binding);
  EXPECT_TRUE(inventory->primary.conflict);
}

TEST(ClientSettingsTests, AppliesAndResetsKeyBindCommands) {
  arv::platform::InputBindings bindings = arv::platform::default_input_bindings();
  const arv::ui::SetKeyBindCommand set_secondary{
      "MoveForward",
      1U,
      arv::ui::InputBindingView{
          true,
          arv::ui::InputBindingDevice::Keyboard,
          "Z",
          "Z",
          0U}};

  ASSERT_TRUE(arv::client::apply_key_bind(bindings, set_secondary));
  const auto& rebound = arv::platform::bindings_for_action(
      bindings,
      arv::platform::LogicalAction::MoveForward);
  ASSERT_GE(rebound.size(), 2U);
  EXPECT_EQ(rebound[1].input_code, "Z");

  const arv::ui::SetKeyBindCommand clear_secondary{"MoveForward", 1U, std::nullopt};
  ASSERT_TRUE(arv::client::apply_key_bind(bindings, clear_secondary));
  const auto& cleared = arv::platform::bindings_for_action(
      bindings,
      arv::platform::LogicalAction::MoveForward);
  ASSERT_EQ(cleared.size(), 1U);

  ASSERT_TRUE(arv::client::apply_key_bind(bindings, set_secondary));
  ASSERT_TRUE(arv::client::reset_key_bind(bindings, {"MoveForward"}));
  const arv::platform::InputBindings defaults = arv::platform::default_input_bindings();
  EXPECT_EQ(
      arv::platform::bindings_for_action(
          bindings,
          arv::platform::LogicalAction::MoveForward),
      arv::platform::bindings_for_action(
          defaults,
          arv::platform::LogicalAction::MoveForward));
}

TEST(ClientSettingsTests, NormalizesRenderSettingsRanges) {
  arv::ui::RenderSettingsView settings;
  settings.fps_cap = 500U;
  settings.load_distance = 99;
  settings.simulation_distance = 64;
  settings.stream_distance = 64;
  settings.view_distance = 64;
  settings.mesh_upload_budget = 0U;
  settings.star_intensity = -1.0F;
  settings.cloud_intensity = 4.0F;
  settings.cirrus_amount = -0.5F;
  settings.cumulus_amount = 2.0F;
  settings.outline_red = -1.0F;
  settings.outline_green = 2.0F;
  settings.outline_blue = 0.5F;
  settings.outline_thickness = 9.0F;

  const arv::ui::RenderSettingsView normalized =
      arv::client::normalized_render_settings(settings);

  EXPECT_EQ(normalized.fps_cap, 240U);
  EXPECT_EQ(normalized.load_distance, 32);
  EXPECT_EQ(normalized.simulation_distance, 32);
  EXPECT_EQ(normalized.stream_distance, 32);
  EXPECT_EQ(normalized.view_distance, 32);
  EXPECT_EQ(normalized.mesh_upload_budget, 1U);
  EXPECT_FLOAT_EQ(normalized.star_intensity, 0.0F);
  EXPECT_FLOAT_EQ(normalized.cloud_intensity, 3.0F);
  EXPECT_FLOAT_EQ(normalized.cirrus_amount, 0.0F);
  EXPECT_FLOAT_EQ(normalized.cumulus_amount, 1.0F);
  EXPECT_FLOAT_EQ(normalized.outline_red, 0.0F);
  EXPECT_FLOAT_EQ(normalized.outline_green, 1.0F);
  EXPECT_FLOAT_EQ(normalized.outline_blue, 0.5F);
  EXPECT_FLOAT_EQ(normalized.outline_thickness, 2.0F);
}

TEST(ClientSettingsTests, NormalizesControlsAndSensitivityConversions) {
  EXPECT_FLOAT_EQ(
      arv::client::normalized_controls_settings({2.0F}).mouse_sensitivity_percent,
      10.0F);
  EXPECT_FLOAT_EQ(
      arv::client::normalized_controls_settings({999.0F}).mouse_sensitivity_percent,
      300.0F);

  EXPECT_FLOAT_EQ(
      arv::client::mouse_sensitivity_radians_from_percent(250.0F, 0.002F),
      0.005F);
  EXPECT_FLOAT_EQ(
      arv::client::mouse_sensitivity_percent_from_radians(0.003F, 0.001F),
      300.0F);
  EXPECT_FLOAT_EQ(
      arv::client::mouse_sensitivity_percent_from_radians(0.003F, 0.0F),
      100.0F);
}

TEST(ClientSettingsTests, NormalizesInterfaceSettingsRanges) {
  arv::ui::InterfaceSettingsView settings;
  settings.chat_history_max = 10U;
  settings.chat_open_lines = 0U;
  settings.chat_passive_lines = 30U;
  settings.chat_passive_visible_seconds = -2.0F;
  settings.chat_passive_fade_seconds = 45.0F;

  const arv::ui::InterfaceSettingsView normalized =
      arv::client::normalized_interface_settings(settings);

  EXPECT_EQ(normalized.chat_history_max, 100U);
  EXPECT_EQ(normalized.chat_open_lines, 1U);
  EXPECT_EQ(normalized.chat_passive_lines, 16U);
  EXPECT_FLOAT_EQ(normalized.chat_passive_visible_seconds, 0.0F);
  EXPECT_FLOAT_EQ(normalized.chat_passive_fade_seconds, 30.0F);
}

TEST(ClientSettingsTests, ProjectsSettingsViewsToRuntimeConfigs) {
  arv::ui::InterfaceSettingsView interface_settings;
  interface_settings.chat_history_max = 42U;
  interface_settings.chat_open_lines = 4U;
  interface_settings.chat_passive_lines = 3U;
  interface_settings.chat_passive_visible_seconds = 1.5F;
  interface_settings.chat_passive_fade_seconds = 0.75F;

  const arv::io::UiConfig::ChatConfig chat_config =
      arv::client::chat_config_from_interface_settings(interface_settings);
  EXPECT_EQ(chat_config.max_history_messages, 100U);
  EXPECT_EQ(chat_config.open_max_lines, 4U);
  EXPECT_EQ(chat_config.passive_max_lines, 3U);
  EXPECT_FLOAT_EQ(chat_config.passive_visible_seconds, 1.5F);
  EXPECT_FLOAT_EQ(chat_config.passive_fade_seconds, 0.75F);

  arv::ui::RenderSettingsView render_settings;
  render_settings.star_intensity = 0.5F;
  render_settings.cloud_intensity = 1.5F;
  render_settings.cirrus_amount = 0.25F;
  render_settings.cumulus_amount = 0.75F;
  render_settings.outline_red = 0.1F;
  render_settings.outline_green = 0.2F;
  render_settings.outline_blue = 0.3F;
  render_settings.outline_thickness = 0.4F;

  const arv::io::RenderConfig::SkyConfig sky_config =
      arv::client::sky_config_from_render_settings(render_settings);
  EXPECT_FLOAT_EQ(sky_config.star_intensity, 0.5F);
  EXPECT_FLOAT_EQ(sky_config.cloud_intensity, 1.5F);
  EXPECT_FLOAT_EQ(sky_config.cirrus_amount, 0.25F);
  EXPECT_FLOAT_EQ(sky_config.cumulus_amount, 0.75F);

  const arv::io::RenderConfig::BlockOutlineConfig outline_config =
      arv::client::block_outline_config_from_render_settings(render_settings);
  EXPECT_FLOAT_EQ(outline_config.color_r, 0.1F);
  EXPECT_FLOAT_EQ(outline_config.color_g, 0.2F);
  EXPECT_FLOAT_EQ(outline_config.color_b, 0.3F);
  EXPECT_FLOAT_EQ(outline_config.thickness, 0.4F);
}

TEST(ClientSettingsTests, ProjectsControlsSettingsToPlayerTuning) {
  arv::player::PlayerTuning base_tuning = arv::player::default_player_tuning();
  base_tuning.walk_speed = 12.0F;
  base_tuning.reach_distance = 9.0F;

  const arv::player::PlayerTuning tuning =
      arv::client::player_tuning_from_controls_settings(
          arv::ui::ControlsSettingsView{250.0F},
          base_tuning,
          0.002F);

  EXPECT_FLOAT_EQ(tuning.mouse_sensitivity_radians, 0.005F);
  EXPECT_FLOAT_EQ(tuning.walk_speed, 12.0F);
  EXPECT_FLOAT_EQ(tuning.reach_distance, 9.0F);
}

} // namespace
