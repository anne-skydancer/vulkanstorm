// Bounded by real scene objects; no additional identities or synthetic nodes.
// LGPL-2.1, like the viewer.
#pragma once
#include "stdtypes.h"
#include <cstddef>
#include <limits>
#include <set>
#include <utility>

template<class Identity> class VSObjectIndex
{
public:
    using IdentitySet = std::set<std::pair<Identity,U32>>;
    using ChildSet = std::set<std::pair<U32,U32>>;
    template<class Object> void replace(U32 local,const Object* previous,const Object& next)
    {
        if (!previous || previous->id != next.id)
        {
            if (previous) mIdentities.erase({previous->id,local});
            mIdentities.emplace(next.id,local);
        }
        if (!previous || previous->parent != next.parent)
        {
            if (previous) mChildren.erase({previous->parent,local});
            mChildren.emplace(next.parent,local);
        }
    }
    template<class Object> void erase(U32 local,const Object& value)
    {
        mIdentities.erase({value.id,local});
        mChildren.erase({value.parent,local});
    }
    auto identities(const Identity& id)const
    {
        return std::make_pair(mIdentities.lower_bound({id,0}),
                             mIdentities.upper_bound({id,std::numeric_limits<U32>::max()}));
    }
    auto children(U32 parent)const
    {
        return std::make_pair(mChildren.lower_bound({parent,0}),
                             mChildren.upper_bound({parent,std::numeric_limits<U32>::max()}));
    }
    void clear(){mIdentities.clear();mChildren.clear();}
    std::size_t size()const{return mIdentities.size();}
private:
    IdentitySet mIdentities;
    ChildSet mChildren;
};
