// Actual per-account object cost capability requests. LGPL-2.1.
#pragma once
#include "vsworldscene.h"
#include <memory>
#include <boost/signals2.hpp>
class VSNativeSession;
struct VSObjectCostValues;
class VSObjectCosts
{
public:
    explicit VSObjectCosts(std::weak_ptr<VSNativeSession>);
    ~VSObjectCosts();
    void request(const VSWorldScene::ObjectHandle&);
    void tick();
    void reset();
    using Completion = boost::signals2::signal<void(const VSWorldScene::ObjectHandle&,const VSObjectCostValues&)>;
    boost::signals2::connection observe(const Completion::slot_type&);

private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
