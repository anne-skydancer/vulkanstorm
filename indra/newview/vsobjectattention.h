// Account-owned native peer for the original viewer attention effects. LGPL-2.1.
#pragma once
#include "vsworldscene.h"
#include "v3math.h"
#include <memory>
#include <optional>
class VSNativeSession;
class VSObjectAttention
{
public:
    explicit VSObjectAttention(std::weak_ptr<VSNativeSession>);
    ~VSObjectAttention();
    bool look(U8,const std::optional<VSWorldScene::ObjectHandle>& = {},LLVector3 = {});
    bool point(U8,const std::optional<VSWorldScene::ObjectHandle>& = {},LLVector3 = {});
    void tick();
    void reset();
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
