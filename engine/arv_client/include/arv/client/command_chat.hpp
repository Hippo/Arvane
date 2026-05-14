#pragma once

namespace arv::command {
struct CommandResult;
}

namespace arv::text {
class LanguageCatalog;
}

namespace arv::client {

class ClientRuntime;

void append_command_result_to_chat(
    ClientRuntime& client,
    const command::CommandResult& result,
    const text::LanguageCatalog& language);

} // namespace arv::client
