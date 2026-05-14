#include "arv/types.h"

#include <string.h>

ArvStringView arv_string_view(const char* data, size_t size) {
  ArvStringView view;
  view.data = data;
  view.size = data == NULL ? 0u : size;
  return view;
}

ArvStringView arv_cstr(const char* text) {
  return arv_string_view(text, text == NULL ? 0u : strlen(text));
}
