inline LLVector3 polar_to_cart(F32 latitude, F32 longitude)
{
    return LLVector3(sin(F_TWO_PI * latitude) * cos(F_TWO_PI * longitude),
                     sin(F_TWO_PI * latitude) * sin(F_TWO_PI * longitude),
                     cos(F_TWO_PI * latitude));
}