#include <arv/api.h>

int arv_plugin_sdk_c_compile_test(void) {
  ArvPlayerSelection selection = {0};
  const ArvPlayersAPI* players = 0;
  ArvChatMessageHandle chat_message = {0};
  const ArvChatMessageAPI* chat_messages = 0;
  const ArvVariablesAPI* variables = 0;
  (void)selection;
  (void)players;
  (void)chat_message;
  (void)chat_messages;
  (void)variables;
  return ARV_API_VERSION == 1u ? 0 : 1;
}
