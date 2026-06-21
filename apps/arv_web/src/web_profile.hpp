#pragma once

#include "arv/client/local_identity.hpp"
#include "arv/core/expected.hpp"

namespace arv::web {

void refresh_web_profile_ui();
[[nodiscard]] bool update_web_profile_name(const char* display_name);
[[nodiscard]] bool import_web_profile_skin(const char* source_png, bool slim_model);
[[nodiscard]] bool set_web_profile_skin_model(bool slim_model);
[[nodiscard]] Expected<client::LocalPlayerIdentity, client::LocalIdentityError>
load_web_local_identity();

} // namespace arv::web
