#pragma once

#include <string>
#include <string_view>

namespace arv::web {

enum class WebShellState {
  Boot,
  Connect,
  Connecting,
  Game,
};

void set_status(const char* text);
void set_status(const std::string& text);

[[nodiscard]] bool storage_ready();
void flush_storage();
void note_runtime_ready();
void resize_canvas();
void request_pointer_lock();
void release_pointer_lock();
void set_shell_state(WebShellState state);
[[nodiscard]] bool open_url(std::string_view url);
[[nodiscard]] bool copy_text_to_clipboard(std::string_view text);
[[nodiscard]] double device_pixel_ratio();
[[nodiscard]] bool pointer_lock_uses_raw_movement();

} // namespace arv::web
