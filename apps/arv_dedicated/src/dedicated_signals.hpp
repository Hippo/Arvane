#pragma once

namespace arv::dedicated {

void install_shutdown_signal_handlers();
[[nodiscard]] bool shutdown_requested() noexcept;
void request_shutdown() noexcept;

} // namespace arv::dedicated
