// Original Build selected-face value policy over genuine renderer owners. LGPL-2.1.
#pragma once
#include "llselectmgr.h"
#include <type_traits>
#include <utility>

template<class Selector,class Getter,class T>
bool vs_build_te_value(Selector* selector,Getter&& getter,T& result,bool hasTolerance=false,T tolerance=T())
{
    if constexpr(std::is_same_v<Selector,LLSelectMgr>)
    {
        struct Visitor final:LLSelectedTEGetFunctor<T>
        {
            Getter& getValue;
            explicit Visitor(Getter& value):getValue(value){}
            T get(LLViewerObject* object,S32 face)override{return getValue(object,face);}
        } visitor(getter);
        return selector->getSelection()->getSelectedTEValue(&visitor,result,hasTolerance,tolerance);
    }
    else
    {
        const auto selection=selector->nativeSelection();
        if(!selection)return true;
        const auto primary=selection->primaryObject();
        bool haveFirst=false,haveSelected=false,identical=true;T selectedValue=T();
        for(auto iter=selector->begin();iter!=selector->end();++iter)
        {
            auto* node=*iter;auto* object=node->getObject();
            const auto metadata=selection->node(node->source);
            if(!object || !metadata)continue;
            const S32 selectedFace=primary && primary->id==object->getID()?metadata->lastFace:-1;
            for(S32 face=0;face<object->getNumTEs();++face)
            {
                if(face>=32 || !(metadata->faceMask & (U32(1)<<face)))continue;
                const T value=getter(object,face);
                if(!haveFirst)
                {
                    haveFirst=true;if(!haveSelected)selectedValue=value;
                }
                else
                {
                    if(value!=selectedValue && (!hasTolerance || !LLCheckIdenticalFunctor<T>::same(value,selectedValue,tolerance)))identical=false;
                    if(face==selectedFace){selectedValue=value;haveSelected=true;}
                }
            }
            if(!identical && haveSelected)break;
        }
        if(haveFirst || haveSelected)result=selectedValue;
        return identical;
    }
}
