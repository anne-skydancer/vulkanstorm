// Development-only replay through the ordinary viewer lifecycle. LGPL-2.1.
#pragma once
#include <string>
bool vs_native_replay_tick();

// Override only the transport endpoint for an explicitly enabled loopback fixture.
std::string vs_native_replay_login_uri(const std::string& selected);
