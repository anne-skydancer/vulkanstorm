// Original LLVOWLSky reference math: Copyright (C) 2010 Linden Research, Inc. LGPL-2.1.
#include <cassert>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cstring>
using U32=uint32_t;using U16=uint16_t;using S32=int32_t;using F32=float;
constexpr float F_PI=3.14159265358979323846f,F_TWO_PI=2*F_PI;
#define llassert(x) assert(x)
struct LLVector3{float v[3];LLVector3(float x=0,float y=0,float z=0):v{x,y,z}{}};
struct LLVector2{float v[2];LLVector2(float x=0,float y=0):v{x,y}{}};
inline F32 calcPhi(const U32 &i, const F32 &reciprocal_num_stacks)
{
    // Calc: PI/8 * 1-((1-t^4)*(1-t^4))  { 0<t<1 }
    // Demos: \pi/8*\left(1-((1-x^{4})*(1-x^{4}))\right)\ \left\{0<x\le1\right\}

    // i should range from [0..SKY_STACKS] so t will range from [0.f .. 1.f]
    F32 t = float(i) * reciprocal_num_stacks; //SL-16127: remove: / float(getNumStacks());

    // ^4 the parameter of the tesselation to bias things toward 0 (the dome's apex)
    t *= t;
    t *= t;

    // invert and square the parameter of the tesselation to bias things toward 1 (the horizon)
    t = 1.f - t;
    t = t*t;
    t = 1.f - t;

    return (F_PI / 8.f) * t;
}
void originalStrips(U32 begin_stack,
                                  U32 end_stack,
                                  LLVector3* & vertices,
                                  LLVector2* & texCoords,
                                  U16* & indices,
                                  const F32 dome_radius,
                                  const U32& num_slices,
                                  const U32& num_stacks)
{
    U32 i, j;
    F32 phi0, theta, x0, y0, z0;
    const F32 reciprocal_num_stacks = 1.f / num_stacks;

    llassert(end_stack <= num_stacks);

    // stacks are iterated one-indexed since phi(0) was handled by the fan above
#if NEW_TESS
    for(i = begin_stack; i <= end_stack; ++i)
#else
    for(i = begin_stack + 1; i <= end_stack+1; ++i)
#endif
    {
        phi0 = calcPhi(i, reciprocal_num_stacks);

        for(j = 0; j < num_slices; ++j)
        {
            theta = F_TWO_PI * (float(j) / float(num_slices));

            // standard transformation from  spherical to
            // rectangular coordinates
            x0 = sin(phi0) * cos(theta);
            y0 = cos(phi0);
            z0 = sin(phi0) * sin(theta);

#if NEW_TESS
            *vertices++ = LLVector3(x0 * dome_radius, y0 * dome_radius, z0 * dome_radius);
#else
            if (i == num_stacks-2)
            {
                *vertices++ = LLVector3(x0*dome_radius, y0*dome_radius-1024.f*2.f, z0*dome_radius);
            }
            else if (i == num_stacks-1)
            {
                *vertices++ = LLVector3(0, y0*dome_radius-1024.f*2.f, 0);
            }
            else
            {
                *vertices++     = LLVector3(x0 * dome_radius, y0 * dome_radius, z0 * dome_radius);
            }
#endif

            // generate planar uv coordinates
            // note: x and z are transposed in order for things to animate
            // correctly in the global coordinate system where +x is east and
            // +y is north
            *texCoords++    = LLVector2((-z0 + 1.f) / 2.f, (-x0 + 1.f) / 2.f);
        }
    }

    //build triangle strip...
    *indices++ = 0 ;

    S32 k = 0 ;
    for(i = 1; i <= end_stack - begin_stack; ++i)
    {
        *indices++ = i * num_slices + k ;

        k = (k+1) % num_slices ;
        for(j = 0; j < num_slices ; ++j)
        {
            *indices++ = (i-1) * num_slices + k ;
            *indices++ = i * num_slices + k ;

            k = (k+1) % num_slices ;
        }

        if((--k) < 0)
        {
            k = num_slices - 1 ;
        }

        *indices++ = i * num_slices + k ;
    }
}
SHARED_KERNEL

int main(){for(U32 stacks:{8u,16u,64u,180u})for(U32 first:{0u,stacks/2}){
 U32 slices=2*stacks,last=stacks;size_t n=(last-first+1)*slices,ni=1+(last-first)*(2+2*slices);
 std::vector<LLVector3>a(n),b(n);std::vector<LLVector2>u(n),v(n);std::vector<U16>i(ni);std::vector<unsigned>j(ni);
 auto*av=a.data();auto*bv=b.data();auto*uv=u.data();auto*vv=v.data();auto*iv=i.data();auto*jv=j.data();
 originalStrips(first,last,av,uv,iv,15000,slices,stacks);vs_sky_strips(first,last,bv,vv,jv,15000,slices,stacks);
 assert(size_t(av-a.data())==n && size_t(bv-b.data())==n);assert(size_t(iv-i.data())==ni && size_t(jv-j.data())==ni);
 assert(!std::memcmp(a.data(),b.data(),n*sizeof(LLVector3)));assert(!std::memcmp(u.data(),v.data(),n*sizeof(LLVector2)));
 for(unsigned k=0;k<ni;++k)assert(i[k]==j[k] && j[k]<n);
 }}