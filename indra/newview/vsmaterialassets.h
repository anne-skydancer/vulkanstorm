// Region-scoped native legacy material acquisition. LGPL-2.1.
#pragma once
#include "llhost.h"
#include "llmaterial.h"
#include "llmaterialid.h"
#include "vsworldscene.h"
#include <map>
#include <memory>
class VSMaterialAssets
{
public:
    explicit VSMaterialAssets(const LLHost&);
    ~VSMaterialAssets();
    LLMaterialPtr get(const LLMaterialID&);
    void reset();
    void requestAll();
    bool loaded() const;
    const std::map<LLMaterialID,LLMaterialPtr>& materials() const;
    struct PreparedPut;
    std::shared_ptr<PreparedPut> preparePut(const VSWorldScene::ObjectHandle&,U8,const LLMaterial&);
    bool put(const std::shared_ptr<PreparedPut>&);
    bool put(const VSWorldScene::ObjectHandle&,U8,const LLMaterial&);
    void tick();
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
