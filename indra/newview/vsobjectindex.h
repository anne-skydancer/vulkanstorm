// Bounded by real scene objects; no additional identities or synthetic nodes.
// LGPL-2.1, like the viewer.
#pragma once
#include "stdtypes.h"
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <utility>

template<class Identity> class VSObjectIndex
{
public:
    using IdentitySet = std::set<std::pair<Identity,U32>>;
    using ChildSet = std::map<std::pair<U32,U64>,U32>;
    template<class Object> void replace(U32 local,const Object* previous,const Object& next)
    {
        if (!previous || previous->id != next.id)
        {
            if (previous) mIdentities.erase({previous->id,local});
            mIdentities.emplace(next.id,local);
        }
        if (!previous || previous->parent != next.parent || previous->id != next.id || reattached(*previous,next))
        {
            if (previous) eraseChild(previous->parent,local);
            if(mNextChildOrder==std::numeric_limits<U64>::max())compactChildOrder();
            const U64 order=mNextChildOrder++;
            mChildren.emplace(std::make_pair(next.parent,order),local);mChildOrder[local]=order;
        }
    }
    template<class Object> void erase(U32 local,const Object& value)
    {
        mIdentities.erase({value.id,local});
        eraseChild(value.parent,local);
    }
    auto identities(const Identity& id)const
    {
        return std::make_pair(mIdentities.lower_bound({id,0}),
                             mIdentities.upper_bound({id,std::numeric_limits<U32>::max()}));
    }
    auto children(U32 parent)const
    {
        return std::make_pair(mChildren.lower_bound({parent,0}),
                             mChildren.upper_bound({parent,std::numeric_limits<U64>::max()}));
    }
    void clear(){mIdentities.clear();mChildren.clear();mChildOrder.clear();mNextChildOrder=1;}
    std::size_t size()const{return mIdentities.size();}
private:
    IdentitySet mIdentities;
    ChildSet mChildren;
    std::map<U32,U64> mChildOrder;
    U64 mNextChildOrder=1;
    template<class Object> static auto parentGeneration(const Object& value,int)->decltype(value.parentGeneration){return value.parentGeneration;}
    template<class Object> static U64 parentGeneration(const Object&,long){return 0;}
    template<class Object> static auto parentRetired(const Object& value,int)->decltype(value.parentRetired){return value.parentRetired;}
    template<class Object> static bool parentRetired(const Object&,long){return false;}
    template<class Object> static bool reattached(const Object& before,const Object& after)
    {
        const U64 previous=parentGeneration(before,0);
        return (parentRetired(before,0) && !parentRetired(after,0)) || (previous && previous!=parentGeneration(after,0));
    }
    void eraseChild(U32 parent,U32 local)
    {
        const auto order=mChildOrder.find(local);
        if(order!=mChildOrder.end()){mChildren.erase({parent,order->second});mChildOrder.erase(order);}
    }
    void compactChildOrder()
    {
        ChildSet ordered;U64 next=1;
        for(const auto& edge:mChildren)
        {ordered.emplace(std::make_pair(edge.first.first,next),edge.second);mChildOrder.at(edge.second)=next++;}
        mChildren.swap(ordered);mNextChildOrder=next;
    }
};
