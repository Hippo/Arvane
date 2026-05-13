#include <arv/plugin.hpp>

ARV_MAIN(plugin) {
  return static_cast<bool>(plugin.commands().register_executor(
      {
          .executor_id = "commands:example",
          .name = "examplecommand",
          .domain = arv::CommandDomain::ServerBound,
      },
      [](arv::CommandContext& ctx) {
        ctx.success("Example command ran.");
      }));
}
