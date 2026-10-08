// Independent CPU raster oracle for native viewer UI qualification. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
std::vector<std::uint8_t> vs_ui_expected_pixels(unsigned width, unsigned height, float dpi,
                                                const std::vector<VSUIRenderer::Packet> &packets);
