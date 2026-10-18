// Frozen original viewer algorithm; LGPL-2.1, like the viewer.
// Frozen LLPanelPrimMediaControls::updateShape 8-corner order, LGPL-2.1.
template<class Transform>
std::array<LLVector3,8> referenceBounds(const LLVector3& minimum,const LLVector3& maximum,const Transform& transform)
{
    LLVector3 ext[2]{minimum,maximum};
            LLVector3 center = (ext[0]+ext[1])*0.5f;
            LLVector3 size = (ext[1]-ext[0])*0.5f;
            LLVector3 vert[] =
            {
                center + size.scaledVec(LLVector3(1,1,1)),
                center + size.scaledVec(LLVector3(-1,1,1)),
                center + size.scaledVec(LLVector3(1,-1,1)),
                center + size.scaledVec(LLVector3(-1,-1,1)),
                center + size.scaledVec(LLVector3(1,1,-1)),
                center + size.scaledVec(LLVector3(-1,1,-1)),
                center + size.scaledVec(LLVector3(1,-1,-1)),
                center + size.scaledVec(LLVector3(-1,-1,-1)),
            };


    std::array<LLVector3,8> result;
    for(U32 i=0;i<8;++i)result[i]=transform(vert[i]);
    return result;
}
