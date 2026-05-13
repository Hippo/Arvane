#include <arvane/plugin/api.h>

int arv_plugin_sdk_c_compile_test(void) {
  return ARV_PLUGIN_API_VERSION == 1u ? 0 : 1;
}
