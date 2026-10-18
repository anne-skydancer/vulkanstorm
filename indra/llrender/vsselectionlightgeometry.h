// The original selected light sphere passes. LGPL-2.1, like the viewer.
#pragma once
template<class Paint>
void vs_selected_light_geometry(Paint& paint,const float* center,float radius,const float* color)
{
    paint.pushMatrix();paint.translatef(center[0],center[1],center[2]);paint.scalef(radius,radius,radius);
    paint.color4fv(color);
    paint.sphere(); // Original outside pass, back-face culled.
    paint.cullFace(0x0404);paint.sphere(); // GL_FRONT, the original inside pass.
    paint.cullFace(0x0405);paint.popMatrix(); // Restore GL_BACK.
}
