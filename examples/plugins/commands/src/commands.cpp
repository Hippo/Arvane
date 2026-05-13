#include <arvane/plugin/api.h>

namespace {

ArvPluginResult command(ArvPluginCommandContext* ctx, void*) {
  ctx->reply(ctx, ARV_PLUGIN_COMMAND_MESSAGE_SUCCESS, arv_cstr("Example command ran."));
  return {ARV_PLUGIN_RESULT_OK, ""};
}

}

extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == nullptr || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  ArvPluginCommandDescriptor descriptor{};
  descriptor.executor_id = arv_cstr("commands:example");
  descriptor.command_name = arv_cstr("examplecommand");
  descriptor.domain = ARV_PLUGIN_COMMAND_SERVER_BOUND;
  descriptor.execute = &command;
  return api->commands->register_executor(api->commands_ctx, &descriptor).code ==
         ARV_PLUGIN_RESULT_OK;
}
