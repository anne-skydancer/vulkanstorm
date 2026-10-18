// Original viewer volume vertex/frustum policy. LGPL-2.1.
#pragma once
#include "llvolume.h"
#include "llmatrix4a.h"
template<class PointTest>
bool vs_volume_vertices_visible(const LLVolume& volume,const LLMatrix4& relative,const LLMatrix4& render,
                                bool active,bool allVerts,PointTest&& pointTest)
{
    LLMatrix4a renderMatrix;renderMatrix.loadu(render);
    LLMatrix4a matrix;matrix.loadu(relative);
    const S32 numFaces=volume.getNumVolumeFaces();
    for(S32 i=0;i<numFaces;++i)
    {
        const LLVolumeFace& face=volume.getVolumeFace(i);
        for(S32 v=0;v<face.mNumVertices;++v)
        {
            const LLVector4a& source=face.mPositions[v];
            LLVector4a vertex;matrix.affineTransform(source,vertex);
            if(active){LLVector4a local=vertex;renderMatrix.affineTransform(local,vertex);}
            const bool inside=pointTest(LLVector3(vertex.getF32ptr()));
            if((!inside && allVerts) || (inside && !allVerts))return !allVerts;
        }
    }
    return allVerts;
}
