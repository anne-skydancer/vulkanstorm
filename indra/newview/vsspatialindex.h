// Backend-neutral native spatial candidate ownership. LGPL-2.1.
#pragma once
#include "vsresourcebudget.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// This is a broadphase only: the actual viewer frustum/volume/permission
// controllers must still test each returned source. No displayed packet,
// texture or successful GPU upload is a membership prerequisite.
class VSSpatialIndex
{
public:
    using Key=std::uint32_t;
    using Bounds=std::array<std::array<float,3>,2>;
    using Reserve=std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>;
    struct Query
    {
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::vector<Key> keys;
    };
    explicit VSSpatialIndex(Reserve);
    ~VSSpatialIndex();
    VSSpatialIndex(const VSSpatialIndex&)=delete;
    VSSpatialIndex&operator=(const VSSpatialIndex&)=delete;
    bool upsert(Key,const Bounds&);
    bool erase(Key);
    void clear();
    bool valid()const;
    std::size_t size()const;
    std::optional<Query> query(const Bounds&)const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
