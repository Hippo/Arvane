#pragma once

#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/io/config.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/base/ui.hpp"

#include <chrono>
#include <vector>

namespace arv::desktop {

[[nodiscard]] std::vector<ui::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server);

[[nodiscard]] usize chat_history_max_scroll_offset(
    usize history_size,
    usize max_visible_messages) noexcept;

[[nodiscard]] usize chat_history_scroll_steps(f32 wheel_y) noexcept;

void refresh_gameplay_chat_lines_for_ui(
    std::vector<ui::ChatLineView>& chat_lines,
    const client::ClientRuntime& client,
    const io::UiConfig::ChatConfig& chat_config,
    std::chrono::steady_clock::time_point now,
    usize& history_scroll_offset);

[[nodiscard]] ui::GameplayUiModel gameplay_ui_model_for_ui(
    client::GameplayUiModelInput input);

} // namespace arv::desktop
