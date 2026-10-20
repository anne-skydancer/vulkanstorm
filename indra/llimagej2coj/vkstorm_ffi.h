/**
 * @file vkstorm_ffi.h
 * @brief C ABI exported by the Rust crate indra/rust/vkstorm-ffi.
 *
 * Keep in step with indra/rust/vkstorm-ffi/src/lib.rs.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef VKSTORM_FFI_H
#define VKSTORM_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- JPEG 2000 ---------------------------------------------------------- */

typedef struct VksJ2cMetadata
{
    uint32_t width;
    uint32_t height;
    uint32_t components;
} VksJ2cMetadata;

typedef struct VksJ2cDecoded VksJ2cDecoded;
typedef struct VksJ2cEncoded VksJ2cEncoded;

/* Static, NUL-terminated codec description for the engine-info display. */
const char* vks_j2c_engine_info(void);

/* Parse the main header. Returns 1 on success and fills out, 0 on failure. */
int32_t vks_j2c_metadata(const uint8_t* data, size_t len, VksJ2cMetadata* out);

/* Decode at discard_level using at most max_bytes of data (0 = all), keeping at
 * most max_channels channels from first_channel. Null on failure. */
VksJ2cDecoded* vks_j2c_decode(const uint8_t* data, size_t len, size_t max_bytes,
                              uint32_t discard_level, uint32_t first_channel, uint32_t max_channels);
void vks_j2c_decoded_size(const VksJ2cDecoded* d, uint32_t* width, uint32_t* height,
                          uint32_t* channels, uint32_t* components);
/* Copy width*height*channels interleaved, bottom-up 8-bit samples. 1 on success. */
int32_t vks_j2c_decoded_copy(const VksJ2cDecoded* d, uint8_t* dst, size_t dst_len);
/* COM marker comment, NUL-terminated, valid until freed; null when absent. */
const char* vks_j2c_decoded_comment(const VksJ2cDecoded* d);
void vks_j2c_decoded_free(VksJ2cDecoded* d);

/* Encode width*height*channels interleaved, bottom-up 8-bit samples into a raw
 * J2K codestream with the viewer's upload rules. comment may be null (then
 * "no comment"); reversible selects lossless coding. Null on failure. */
VksJ2cEncoded* vks_j2c_encode(const uint8_t* data, size_t len, uint32_t width, uint32_t height,
                              uint32_t channels, const char* comment, int32_t reversible);
const uint8_t* vks_j2c_encoded_data(const VksJ2cEncoded* e);
size_t vks_j2c_encoded_len(const VksJ2cEncoded* e);
void vks_j2c_encoded_free(VksJ2cEncoded* e);

#ifdef __cplusplus
}
#endif

#endif /* VKSTORM_FFI_H */
