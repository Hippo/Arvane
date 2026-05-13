#include <arvane/plugin/plugin.hpp>

int arv_plugin_sdk_cpp_compile_test() {
  return ARV_PLUGIN_API_VERSION == 1u ? 0 : 1;
}
