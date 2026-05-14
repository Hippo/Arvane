#include <arv/api.h>

int arv_plugin_sdk_c_compile_test(void) {
  ArvPlayerSelection selection = {0};
  const ArvPlayersAPI* players = 0;
  (void)selection;
  (void)players;
  return ARV_API_VERSION == 1u ? 0 : 1;
}
