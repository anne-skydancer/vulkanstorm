// Closed native startup UI admission. LGPL-2.1.
#pragma once
#include <memory>
class VSUIAdmission;
std::unique_ptr<VSUIAdmission> vs_startup_ui_admission();
