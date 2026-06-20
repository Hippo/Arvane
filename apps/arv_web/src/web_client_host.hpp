#pragma once

namespace arv::web {

void initialize_web_client_host() noexcept;
void poll_web_session();
void collect_web_runtime_chunk_stream();
void connect_web_session(const char* url);
void disconnect_web_session();

} // namespace arv::web
