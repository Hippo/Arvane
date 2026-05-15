#include "plugin_manager_internal.hpp"

namespace arv::plugin {

PluginAPIStorage::PluginAPIStorage(PluginCallContext* context) {
  fill_context_api(*this);
  fill_registry_api(*this);
  fill_command_api(*this);
  fill_event_api(*this);
  fill_scheduler_api(*this);
  fill_service_api(*this);
  fill_resource_api(*this);
  fill_world_entity_api(*this);
  fill_data_api(*this);
  fill_text_api(*this);
  fill_permission_api(*this);
  fill_region_api(*this);
  fill_game_mode_api(*this);
  fill_network_api(*this);
  fill_player_api(*this);
  fill_variables_api(*this);
  fill_chat_message_api(*this);

  api = {
      PluginApiVersion,
      context,
      &context_api,
      context,
      &registries_api,
      context,
      &resources_api,
      context,
      &worlds_api,
      context,
      &entities_api,
      context,
      &events_api,
      context,
      &scheduler_api,
      context,
      &commands_api,
      context,
      &permissions_api,
      context,
      &services_api,
      context,
      &data_api,
      context,
      &config_api,
      context,
      &persistent_data_api,
      context,
      &worldgen_api,
      context,
      &network_api,
      context,
      &text_api,
      context,
      &metrics_api,
      context,
      &logger_api,
      context,
      &regions_api,
      context,
      &game_modes_api,
      context,
      &players_api,
      context,
      &variables_api,
      context,
      &chat_message_api,
  };
}

} // namespace arv::plugin
