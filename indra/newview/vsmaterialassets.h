// Region-scoped native legacy material acquisition. LGPL-2.1.
#pragma once
#include "llhost.h"
#include "llmaterial.h"
#include "llmaterialid.h"
#include <memory>
class VSMaterialAssets
{
public:
    explicit VSMaterialAssets(const LLHost&);
    ~VSMaterialAssets();
    LLMaterialPtr get(const LLMaterialID&);
    void reset();
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
