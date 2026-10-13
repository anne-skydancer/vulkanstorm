// Independent CPU raster oracle for native viewer UI qualification. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
// DestinationProducts quantizes fragment RGBA and each blend product to RGBA8.
enum class VSUIBlendPrecision { FloatSum, DestinationProducts };
std::vector<std::uint8_t> vs_ui_expected_pixels(unsigned width, unsigned height, float dpi,
                                                const std::vector<VSUIRenderer::Packet> &packets, unsigned subpixel_bits = 0,
                                                const std::vector<std::uint8_t>& background = {},
                                                VSUIBlendPrecision precision = VSUIBlendPrecision::FloatSum);
bool vs_ui_pixel_matches(const std::uint8_t* actual, const std::uint8_t* floating,
                         const std::uint8_t* destination, unsigned tolerance = 3);
