// Frozen original viewer algorithm; LGPL-2.1, like the viewer.
// Original LLFace math from150552e9a1. LGPL-2.1.
void planarProjection(LLVector2 &tc, const LLVector4a& normal,
                      const LLVector4a &center, const LLVector4a& vec)
{
    LLVector4a binormal;
    F32 d = normal[0];

    if (d >= 0.5f || d <= -0.5f)
    {
        if (d < 0)
        {
            binormal.set(0,-1,0);
        }
        else
        {
            binormal.set(0, 1, 0);
        }
    }
    else
    {
        if (normal[1] > 0)
        {
            binormal.set(-1,0,0);
        }
        else
        {
            binormal.set(1,0,0);
        }
    }
    LLVector4a tangent;
    tangent.setCross3(binormal,normal);

    tc.mV[1] = -((tangent.dot3(vec).getF32())*2 - 0.5f);
    tc.mV[0] = 1.0f+((binormal.dot3(vec).getF32())*2 - 0.5f);
}
static void xform(LLVector2 &tex_coord, F32 cosAng, F32 sinAng, F32 offS, F32 offT, F32 magS, F32 magT)
{
    // New, good way
    F32 s = tex_coord.mV[0];
    F32 t = tex_coord.mV[1];

    // Texture transforms are done about the center of the face.
    s -= 0.5;
    t -= 0.5;

    // Handle rotation
    F32 temp = s;
    s  = s     * cosAng + t * sinAng;
    t  = -temp * sinAng + t * cosAng;

    // Then scale
    s *= magS;
    t *= magT;

    // Then offset
    s += offS + 0.5f;
    t += offT + 0.5f;

    tex_coord.mV[0] = s;
    tex_coord.mV[1] = t;
}
