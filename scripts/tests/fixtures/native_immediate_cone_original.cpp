void LLCone::render(S32 sides)
{
    gGL.begin(LLRender::TRIANGLE_FAN);
    gGL.vertex3f(0,0,0);

    for (S32 i = 0; i < sides; i++)
    {
        F32 a = (F32) i/sides * F_PI*2.f;
        F32 x = cosf(a)*0.5f;
        F32 y = sinf(a)*0.5f;
        gGL.vertex3f(x,y,-.5f);
    }
    gGL.vertex3f(cosf(0.f)*0.5f, sinf(0.f)*0.5f, -0.5f);

    gGL.end();

    gGL.begin(LLRender::TRIANGLE_FAN);
    gGL.vertex3f(0.f, 0.f, 0.5f);
    for (S32 i = 0; i < sides; i++)
    {
        F32 a = (F32) i/sides * F_PI*2.f;
        F32 x = cosf(a)*0.5f;
        F32 y = sinf(a)*0.5f;
        gGL.vertex3f(x,y,-0.5f);
    }
    gGL.vertex3f(cosf(0.f)*0.5f, sinf(0.f)*0.5f, -0.5f);

    gGL.end();
}