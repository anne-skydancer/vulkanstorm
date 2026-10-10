// Native text UI host for the production skinned group directory panel. LGPL-2.1.
#pragma once
#include "llfloater.h"

class VSGroupSearch final : public LLFloater
{
public:
    explicit VSGroupSearch(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
};
void vs_show_group_search();
