#include "web_platform.hpp"

#include "web_application.hpp"
#include "web_render_host.hpp"

#include <emscripten.h>

#include <algorithm>
#include <string>

namespace {

EM_JS(void, arv_web_set_status, (const char* text), {
  const value = UTF8ToString(text);
  if (typeof window.arvWebSetStatus === 'function') {
    window.arvWebSetStatus(value);
  } else {
    console.log(value);
  }
});

EM_JS(void, arv_web_note_runtime_ready, (), {
  if (typeof window.arvWebRuntimeReady === 'function') {
    window.arvWebRuntimeReady();
  }
});

EM_JS(void, arv_web_request_pointer_lock, (), {
  const canvas = document.getElementById('arvane-canvas');
  if (canvas && typeof canvas.requestPointerLock === 'function') {
    Module.arvWebPointerRawMovement = false;
    const requestDefault = function() {
      try {
        const fallback = canvas.requestPointerLock();
        if (fallback && typeof fallback.catch === 'function') {
          fallback.catch(function(error) {
            console.warn('[arv_web] pointer lock failed', error);
          });
        }
      } catch (error) {
        console.warn('[arv_web] pointer lock failed', error);
      }
    };
    try {
      const requested = canvas.requestPointerLock({ unadjustedMovement: true });
      if (requested && typeof requested.then === 'function') {
        requested.then(function() {
          Module.arvWebPointerRawMovement = true;
        }).catch(function() {
          Module.arvWebPointerRawMovement = false;
          requestDefault();
        });
      }
    } catch (error) {
      Module.arvWebPointerRawMovement = false;
      requestDefault();
    }
  }
});

EM_JS(void, arv_web_release_pointer_lock, (), {
  if (document.pointerLockElement && typeof document.exitPointerLock === 'function') {
    document.exitPointerLock();
  }
});

EM_JS(void, arv_web_set_shell_state, (const char* state), {
  const value = UTF8ToString(state);
  if (typeof window.arvWebSetShellState === 'function') {
    window.arvWebSetShellState(value);
  }
});

EM_JS(int, arv_web_open_url, (const char* url), {
  const value = UTF8ToString(url);
  try {
    const opened = window.open(value, '_blank', 'noopener,noreferrer');
    return opened ? 1 : 0;
  } catch (error) {
    console.warn('[arv_web] failed to open URL', error);
    return 0;
  }
});

EM_JS(int, arv_web_copy_text_to_clipboard, (const char* text), {
  const value = UTF8ToString(text);
  if (!navigator.clipboard || typeof navigator.clipboard.writeText !== 'function') {
    return 0;
  }
  try {
    navigator.clipboard.writeText(value).catch(function(error) {
      console.warn('[arv_web] clipboard write failed', error);
    });
    return 1;
  } catch (error) {
    console.warn('[arv_web] clipboard write failed', error);
    return 0;
  }
});

EM_JS(double, arv_web_device_pixel_ratio, (), {
  return window.devicePixelRatio || 1.0;
});

EM_JS(int, arv_web_pointer_lock_uses_raw_movement, (), {
  return document.pointerLockElement && Module && Module.arvWebPointerRawMovement ? 1 : 0;
});

EM_JS(int, arv_web_storage_ready, (), {
  return Module && Module.arvWebStorageReady ? 1 : 0;
});

EM_JS(void, arv_web_flush_storage, (), {
  if (!Module || !Module.arvWebStorageReady || Module.arvWebStorageFlushing) {
    return;
  }
  if (typeof FS === 'undefined' || typeof FS.syncfs !== 'function') {
    return;
  }
  Module.arvWebStorageFlushing = true;
  FS.syncfs(false, function(error) {
    Module.arvWebStorageFlushing = false;
    if (error) {
      Module.arvWebStorageError = String(error);
      if (typeof window.arvWebSetStatus === 'function') {
        window.arvWebSetStatus('storage sync failed: ' + Module.arvWebStorageError);
      }
      return;
    }
    if (typeof window.arvWebSetStatus === 'function') {
      window.arvWebSetStatus('storage synced');
    }
  });
});

EM_JS(int, arv_web_canvas_width, (), {
  const canvas = document.getElementById('arvane-canvas');
  if (!canvas) {
    return 1;
  }
  return Math.max(1, Math.floor(canvas.clientWidth * (window.devicePixelRatio || 1)));
});

EM_JS(int, arv_web_canvas_height, (), {
  const canvas = document.getElementById('arvane-canvas');
  if (!canvas) {
    return 1;
  }
  return Math.max(1, Math.floor(canvas.clientHeight * (window.devicePixelRatio || 1)));
});

EM_JS(void, arv_web_set_canvas_size, (int width, int height), {
  const canvas = document.getElementById('arvane-canvas');
  if (!canvas) {
    return;
  }
  if (canvas.width !== width) {
    canvas.width = width;
  }
  if (canvas.height !== height) {
    canvas.height = height;
  }
});

} // namespace

namespace arv::web {

void set_status(const char* text) {
  arv_web_set_status(text);
}

void set_status(const std::string& text) {
  arv_web_set_status(text.c_str());
}

bool storage_ready() {
  return arv_web_storage_ready() != 0;
}

void flush_storage() {
  arv_web_flush_storage();
}

void note_runtime_ready() {
  arv_web_note_runtime_ready();
}

void request_pointer_lock() {
  arv_web_request_pointer_lock();
}

void release_pointer_lock() {
  arv_web_release_pointer_lock();
}

void set_shell_state(WebShellState state) {
  switch (state) {
    case WebShellState::Boot:
      arv_web_set_shell_state("boot");
      break;
    case WebShellState::Connect:
      arv_web_set_shell_state("connect");
      break;
    case WebShellState::Connecting:
      arv_web_set_shell_state("connecting");
      break;
    case WebShellState::Game:
      arv_web_set_shell_state("game");
      break;
  }
}

bool open_url(std::string_view url) {
  const std::string owned{url};
  return arv_web_open_url(owned.c_str()) != 0;
}

bool copy_text_to_clipboard(std::string_view text) {
  const std::string owned{text};
  return arv_web_copy_text_to_clipboard(owned.c_str()) != 0;
}

double device_pixel_ratio() {
  return std::max(0.25, arv_web_device_pixel_ratio());
}

bool pointer_lock_uses_raw_movement() {
  return arv_web_pointer_lock_uses_raw_movement() != 0;
}

void resize_canvas() {
  const int width = arv_web_canvas_width();
  const int height = arv_web_canvas_height();
  if (width == g_app.width && height == g_app.height) {
    return;
  }

  g_app.width = width;
  g_app.height = height;
  arv_web_set_canvas_size(width, height);
  resize_web_render_host(width, height);
}

} // namespace arv::web
