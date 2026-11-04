/**
 * @file alphaOITPackF.glsl
 * @brief Alpha OIT A-buffer node encoding, shared by the capture and resolve shaders
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of either the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the License or
 * (at your option) any later version, or the GNU General Public License as
 * published by the Free Software Foundation, version 3 of the License or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE,
 * LICENSE-LGPL-2.1, LICENSE-GPL-3.0 and NOTICE.md in the source tree.
 */

// One A-buffer node is an RG32UI texel (8 bytes):
//   x = premultiplied colour as R11F_G11F_B10F (red bits 0..10, green 11..21, blue 22..31)
//   y = window depth as 24-bit unorm in bits 8..31, alpha as 8-bit unorm in bits 0..7
// Depth sits in the high bits, so comparing y words orders nodes by depth. 24 bits is the
// precision of the opaque depth buffer (GL_DEPTH_COMPONENT24) the captured depth is tested
// against; 8 bits is the precision of the texture alpha the fragments come from.

// Unsigned small float: 5-bit exponent (bias 15), mbits mantissa bits, no sign bit, as in
// GL_R11F_G11F_B10F (mbits 6 for red/green, 5 for blue). Zero, negatives and NaN encode as
// 0. Values above the largest finite value (65024 for 6 bits, 64512 for 5) clamp to it
// instead of becoming infinity, so the composite never multiplies an infinity by zero.
// Rounds to nearest, ties to even.
uint oit_pack_ufloat(float v, uint mbits)
{
    if (!(v > 0.0))
    {
        return 0u;
    }
    if (v < 6.103515625e-05)   // 2^-14, the smallest normal: encode a denormal
    {
        // m * 2^-(14 + mbits); a rounding up to 1 << mbits is the bit pattern of 2^-14.
        return uint(roundEven(ldexp(v, int(14u + mbits))));
    }
    uint b = floatBitsToUint(v);
    uint shift = 23u - mbits;
    b += ((1u << (shift - 1u)) - 1u) + ((b >> shift) & 1u);   // a mantissa carry bumps the exponent
    int e = int(b >> 23) - 112;                               // rebias 127 -> 15
    if (e > 30)
    {
        return (30u << mbits) | ((1u << mbits) - 1u);
    }
    return (uint(e) << mbits) | ((b >> shift) & ((1u << mbits) - 1u));
}

float oit_unpack_ufloat(uint p, uint mbits)
{
    uint e = p >> mbits;
    uint m = p & ((1u << mbits) - 1u);
    if (e == 0u)
    {
        return ldexp(float(m), -int(14u + mbits));
    }
    return uintBitsToFloat(((e + 112u) << 23) | (m << (23u - mbits)));
}

uint oit_pack_rgb(vec3 c)
{
    return oit_pack_ufloat(c.r, 6u) | (oit_pack_ufloat(c.g, 6u) << 11) | (oit_pack_ufloat(c.b, 5u) << 22);
}

vec3 oit_unpack_rgb(uint p)
{
    return vec3(oit_unpack_ufloat(p & 0x7FFu, 6u),
                oit_unpack_ufloat((p >> 11) & 0x7FFu, 6u),
                oit_unpack_ufloat(p >> 22, 5u));
}

// Window depth (0..1) as a 24-bit unorm. Monotonic, so z <= z' implies
// oit_depth24(z) <= oit_depth24(z'): nothing the capture kept against the opaque depth is
// rejected by the resolve's quantised test. One rounding (roundEven of the product) rather
// than adding 0.5, which a compiler may fuse into an FMA and round differently; a value read
// from the 24-bit depth buffer maps back to its own code.
uint oit_depth24(float z)
{
    return uint(roundEven(clamp(z, 0.0, 1.0) * 16777215.0));
}

uint oit_pack_depth_alpha(float z, float a)
{
    return (oit_depth24(z) << 8) | uint(roundEven(clamp(a, 0.0, 1.0) * 255.0));
}
