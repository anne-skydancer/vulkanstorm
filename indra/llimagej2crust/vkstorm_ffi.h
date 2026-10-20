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

typedef struct VksJ2cMetadata
{
    uint32_t width;
    uint32_t height;
    uint32_t components;
} VksJ2cMetadata;

typedef struct VksJ2cDecoded VksJ2cDecoded;

const char* vks_j2c_engine_info(void);
int32_t vks_j2c_metadata(const uint8_t* data, size_t len, VksJ2cMetadata* out);
VksJ2cDecoded* vks_j2c_decode(const uint8_t* data, size_t len, size_t max_bytes,
                              uint32_t discard_level, uint32_t first_channel, uint32_t max_channels);
void vks_j2c_decoded_size(const VksJ2cDecoded* d, uint32_t* width, uint32_t* height,
                          uint32_t* channels, uint32_t* components);
int32_t vks_j2c_decoded_copy(const VksJ2cDecoded* d, uint8_t* dst, size_t dst_len);
const char* vks_j2c_decoded_comment(const VksJ2cDecoded* d);
void vks_j2c_decoded_free(VksJ2cDecoded* d);

#ifdef __cplusplus
}
#endif

#endif /* VKSTORM_FFI_H */
