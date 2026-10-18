// Backend-neutral native spatial candidate ownership. LGPL-2.1.
#include "vsspatialindex.h"
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/strategies/index/cartesian.hpp>
#include <boost/geometry/algorithms/covered_by.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <utility>

namespace
{
struct SpatialAllocation
{
    VSSpatialIndex::Reserve reserve;
};
template<class T>class SpatialAllocator
{
    template<class U>friend class SpatialAllocator;
    struct Header{std::shared_ptr<const VSResourceBudget::Reservation> residency;};
    std::shared_ptr<SpatialAllocation> state;
public:
    using value_type=T;
    using pointer=T*;
    using const_pointer=const T*;
    template<class U>struct rebind{using other=SpatialAllocator<U>;};
    explicit SpatialAllocator(std::shared_ptr<SpatialAllocation> owner):state(std::move(owner)){}
    template<class U>SpatialAllocator(const SpatialAllocator<U>& other)noexcept:state(other.state){}
    T* allocate(std::size_t count)
    {
        constexpr std::size_t alignment=std::max(alignof(T),alignof(Header));
        constexpr std::size_t prefix=(sizeof(Header)+alignment-1)/alignment*alignment;
        if(count>(std::numeric_limits<std::size_t>::max()-prefix)/sizeof(T))throw std::bad_alloc();
        const auto bytes=prefix+count*sizeof(T);
        auto residency=state->reserve(bytes);if(!residency)throw std::bad_alloc();
        void* raw=::operator new(bytes,std::align_val_t(alignment));
        new(raw)Header{std::move(residency)};
        return reinterpret_cast<T*>(static_cast<unsigned char*>(raw)+prefix);
    }
    void deallocate(T* value,std::size_t)noexcept
    {
        constexpr std::size_t alignment=std::max(alignof(T),alignof(Header));
        constexpr std::size_t prefix=(sizeof(Header)+alignment-1)/alignment*alignment;
        auto* raw=reinterpret_cast<unsigned char*>(value)-prefix;
        reinterpret_cast<Header*>(raw)->~Header();
        ::operator delete(raw,std::align_val_t(alignment));
    }
    template<class U>bool operator==(const SpatialAllocator<U>& other)const noexcept{return state==other.state;}
    template<class U>bool operator!=(const SpatialAllocator<U>& other)const noexcept{return !(*this==other);}
};
namespace bg=boost::geometry;
namespace bgi=boost::geometry::index;
using Point=bg::model::point<float,3,bg::cs::cartesian>;
using Box=bg::model::box<Point>;
using Value=std::pair<Box,VSSpatialIndex::Key>;
bool boxesEqual(const Box&a,const Box&b)
{
 return bg::get<0,0>(a)==bg::get<0,0>(b) && bg::get<0,1>(a)==bg::get<0,1>(b) && bg::get<0,2>(a)==bg::get<0,2>(b) &&
        bg::get<1,0>(a)==bg::get<1,0>(b) && bg::get<1,1>(a)==bg::get<1,1>(b) && bg::get<1,2>(a)==bg::get<1,2>(b);
}
struct Equal {bool operator()(const Value&a,const Value&b)const{return a.second==b.second && boxesEqual(a.first,b.first);}};
using Allocator=SpatialAllocator<Value>;
using Tree=bgi::rtree<Value,bgi::quadratic<16>,bgi::indexable<Value>,Equal,Allocator>;
using KeyValue=std::pair<const VSSpatialIndex::Key,Box>;
using Keys=std::map<VSSpatialIndex::Key,Box,std::less<VSSpatialIndex::Key>,SpatialAllocator<KeyValue>>;
std::optional<Box> box(const VSSpatialIndex::Bounds& bounds)
{
    for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(bounds[0][axis]) || !std::isfinite(bounds[1][axis]) || bounds[0][axis]>bounds[1][axis])return {};
    return Box(Point(bounds[0][0],bounds[0][1],bounds[0][2]),Point(bounds[1][0],bounds[1][1],bounds[1][2]));
}
}
struct VSSpatialIndex::Impl
{
    Reserve reserve;
    std::shared_ptr<SpatialAllocation> allocation;
    std::unique_ptr<Tree> tree;
    Keys values;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    // R-tree insertion failures can offer only the basic exception guarantee.
    // Retire that acceleration structure, never the authoritative scene.
    bool usable=true;
    explicit Impl(Reserve owner):reserve(std::move(owner)),allocation(std::make_shared<SpatialAllocation>(SpatialAllocation{reserve})),
        values(std::less<Key>(),SpatialAllocator<KeyValue>(allocation))
    {residency=reserve(sizeof(Impl)+sizeof(Tree)+sizeof(SpatialAllocation));if(!residency)throw std::bad_alloc();reset();}
    void reset()
    {
        tree=std::make_unique<Tree>(bgi::quadratic<16>(),bgi::indexable<Value>(),Equal(),Allocator(allocation));
        values.clear();
        usable=true;
    }
    void refused(){tree.reset();values.clear();usable=false;}
};
VSSpatialIndex::VSSpatialIndex(Reserve reserve):mImpl(std::make_unique<Impl>(std::move(reserve))){}
VSSpatialIndex::~VSSpatialIndex()=default;
bool VSSpatialIndex::upsert(Key key,const Bounds& bounds)
{
    if(!mImpl->usable)return false;
    const auto next=box(bounds);if(!next){mImpl->refused();return false;}
    try
    {
        const auto old=mImpl->values.find(key);
        if(old!=mImpl->values.end() && boxesEqual(old->second,*next))return true;
        mImpl->tree->insert({*next,key});
        if(old!=mImpl->values.end())
        {mImpl->tree->remove({old->second,key});old->second=*next;}
        else mImpl->values.emplace(key,*next);
        return true;
    }
    catch(const std::bad_alloc&){mImpl->refused();return false;}
}
bool VSSpatialIndex::erase(Key key)
{
    if(!mImpl->usable)return false;
    try
    {
        const auto old=mImpl->values.find(key);
        if(old!=mImpl->values.end())
        {mImpl->tree->remove({old->second,key});mImpl->values.erase(old);}
        return true;
    }
    catch(const std::bad_alloc&){mImpl->refused();return false;}
}
void VSSpatialIndex::clear(){try {mImpl->reset();}catch(const std::bad_alloc&){mImpl->refused();}}
bool VSSpatialIndex::valid()const{return mImpl->usable;}
std::size_t VSSpatialIndex::size()const{return mImpl->tree?mImpl->tree->size():0;}
std::optional<VSSpatialIndex::Query> VSSpatialIndex::query(const Bounds& bounds)const
{
    if(!mImpl->usable)return {};
    const auto search=box(bounds);if(!search)return {};
    Query result;
    if(mImpl->tree->empty())return result;
    try
    {
        std::size_t count=0;
        for(auto i=mImpl->tree->qbegin(bgi::intersects(*search));i!=mImpl->tree->qend();++i)++count;
        if(!count)return result;
        if(count>std::numeric_limits<std::size_t>::max()/sizeof(Key))return {};
        result.residency=mImpl->reserve(count*sizeof(Key));if(!result.residency)return {};
        result.keys.reserve(count);
        for(auto i=mImpl->tree->qbegin(bgi::intersects(*search));i!=mImpl->tree->qend();++i)result.keys.push_back(i->second);
    }
    catch(const std::bad_alloc&){return {};}
    return result;
}
