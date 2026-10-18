template<class PointTest>bool originalVisible(const LLVolume& volume,LLMatrix4 mat,LLMatrix4 render_mat,bool active,bool all_verts,PointTest pointTest)
{
 S32 i,num_faces;
    LLMatrix4a render_mata;
    render_mata.loadu(render_mat);
    LLMatrix4a mata;
    mata.loadu(mat);

    num_faces = volume.getNumVolumeFaces();
    for (i = 0; i < num_faces; i++)
    {
        const LLVolumeFace& face = volume.getVolumeFace(i);

        for (S32 v = 0; v < face.mNumVertices; v++)
        {
            const LLVector4a& src_vec = face.mPositions[v];
            LLVector4a vec;
            mata.affineTransform(src_vec, vec);

            if (active)
            {
                LLVector4a t = vec;
                render_mata.affineTransform(t, vec);
            }

            bool in_frustum = pointTest(LLVector3(vec.getF32ptr()));

            if (( !in_frustum && all_verts) ||
                 (in_frustum && !all_verts))
            {
                return !all_verts;
            }
        }
    }
    return all_verts;
}