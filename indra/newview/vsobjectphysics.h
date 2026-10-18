// Actual per-account object physics capability requests. LGPL-2.1.
#pragma once
#include "vsworldscene.h"
#include <memory>
class VSNativeSession;
class VSObjectPhysics
{
public:
    explicit VSObjectPhysics(std::weak_ptr<VSNativeSession>);
    ~VSObjectPhysics();
    void request(const VSWorldScene::ObjectHandle&);
    void receive(const LLSD&, const LLHost&);
    void tick();
    void reset();

private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
