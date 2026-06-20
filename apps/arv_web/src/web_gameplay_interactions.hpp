#pragma once

#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/core/types.hpp"

namespace arv::web {

void send_web_entity_interactions_from_input(
    client::InputFrame& input,
    Seconds now_seconds);
void update_web_held_item_animation_from_input(
    const client::InputFrame& input,
    Seconds now_seconds);
void update_web_local_particles_from_input(
    const client::InputFrame& input,
    Seconds now_seconds,
    Seconds delta_seconds);
[[nodiscard]] ui::RidePromptModel web_ride_prompt_model();

} // namespace arv::web
