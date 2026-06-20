#include "web_canvas_context.hpp"

#include <emscripten/html5.h>

#include "web_platform.hpp"
#include "web_render_host.hpp"

namespace arv::web {
namespace {

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_context{};

} // namespace

bool initialize_webgl() {
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = EM_TRUE;
  attributes.depth = EM_TRUE;
  attributes.stencil = EM_FALSE;
  attributes.antialias = EM_TRUE;
  attributes.preserveDrawingBuffer = EM_FALSE;
  attributes.enableExtensionsByDefault = EM_TRUE;
  attributes.majorVersion = 2;
  attributes.minorVersion = 0;

  g_webgl_context = emscripten_webgl_create_context("#arvane-canvas", &attributes);
  if (g_webgl_context <= 0) {
    set_status("failed to create WebGL2 context");
    return false;
  }
  if (emscripten_webgl_make_context_current(g_webgl_context) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    set_status("failed to activate WebGL2 context");
    return false;
  }
  if (!initialize_web_render_host()) {
    return false;
  }
  resize_canvas();
  set_status("WebGL2 ready");
  return true;
}

bool web_canvas_context_ready() noexcept {
  return g_webgl_context > 0;
}

} // namespace arv::web
