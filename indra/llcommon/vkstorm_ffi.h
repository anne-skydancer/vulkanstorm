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

/* ---- LLSD serialization ------------------------------------------------- */

/* Format ids. */
#define VKS_LLSD_NOTATION 0
#define VKS_LLSD_BINARY 1
#define VKS_LLSD_XML 2

/* A byte source for the parsers. read pulls up to cap bytes and returns the
 * count (0 = end of input). peek returns the next byte or -1 and is only used
 * when exact is set; then the parser never reads ahead of what it consumes,
 * so a stream is left positioned right after the parsed value. */
typedef size_t (*VksLlsdRead)(void* ctx, uint8_t* buf, size_t cap);
typedef int32_t (*VksLlsdPeek)(void* ctx);

typedef struct VksLlsdSource
{
    void* ctx;
    VksLlsdRead read;
    VksLlsdPeek peek;
    int32_t exact;
} VksLlsdSource;

/* The outcome of a parse: the LLSDParser::parse() return value, the bytes the
 * parser consumed and the bytes it pulled but left unconsumed (to seek back). */
typedef struct VksLlsdParseResult
{
    int32_t count;
    size_t consumed;
    size_t unconsumed;
} VksLlsdParseResult;

/* One node of a parsed document. len is the entry count of a map or array
 * and the byte length of a string, URI, binary, key or UUID (16). payload is
 * the integer or boolean (as int64), the real or date (double bits), or the
 * byte offset into the document's arena for the length-carrying kinds. A map's
 * entries follow it as key node, value subtree pairs; an array's as subtrees. */
typedef struct VksLlsdNode
{
    uint8_t kind;
    uint8_t pad[3];
    uint32_t len;
    uint64_t payload;
} VksLlsdNode;

#define VKS_LLSD_NODE_UNDEF 0
#define VKS_LLSD_NODE_BOOLEAN 1
#define VKS_LLSD_NODE_INTEGER 2
#define VKS_LLSD_NODE_REAL 3
#define VKS_LLSD_NODE_STRING 4
#define VKS_LLSD_NODE_UUID 5
#define VKS_LLSD_NODE_DATE 6
#define VKS_LLSD_NODE_URI 7
#define VKS_LLSD_NODE_BINARY 8
#define VKS_LLSD_NODE_MAP 9
#define VKS_LLSD_NODE_ARRAY 10
#define VKS_LLSD_NODE_KEY 11

typedef struct VksLlsdDoc VksLlsdDoc;

/* Parse one value of format from src, after the optional prefix bytes.
 * max_bytes < 0 and max_depth < 0 are unlimited; xml_lines selects the
 * parseLines() end-of-input rule. Fills out; returns the document when
 * out->count is positive, else null. */
VksLlsdDoc* vks_llsd_parse(int32_t format, const VksLlsdSource* src, const uint8_t* prefix, size_t prefix_len,
                           int64_t max_bytes, int32_t max_depth, int32_t xml_lines, VksLlsdParseResult* out);
const VksLlsdNode* vks_llsd_doc_nodes(const VksLlsdDoc* d, size_t* count);
const uint8_t* vks_llsd_doc_bytes(const VksLlsdDoc* d, size_t* len);
void vks_llsd_doc_free(VksLlsdDoc* d);

/* Formatting: one formatter per document, driven in document order. */
typedef void (*VksLlsdWrite)(void* ctx, const uint8_t* data, size_t len);
typedef struct VksLlsdFormatter VksLlsdFormatter;

#define VKS_LLSD_OPT_PRETTY 1u
#define VKS_LLSD_OPT_PRETTY_BINARY 2u
#define VKS_LLSD_OPT_BOOLALPHA 4u

/* precision and floatfield (0 general, 1 fixed, 2 scientific) are the
 * destination stream's settings for reals. Null for an unknown format. */
VksLlsdFormatter* vks_llsd_formatter_new(int32_t format, uint32_t options, int32_t precision, int32_t floatfield,
                                         void* ctx, VksLlsdWrite write);
void vks_llsd_fmt_undef(VksLlsdFormatter* f);
void vks_llsd_fmt_boolean(VksLlsdFormatter* f, int32_t v);
void vks_llsd_fmt_integer(VksLlsdFormatter* f, int32_t v);
void vks_llsd_fmt_real(VksLlsdFormatter* f, double v);
/* A real with its text already produced by the formatter's realFormat. */
void vks_llsd_fmt_real_text(VksLlsdFormatter* f, double v, const uint8_t* text, size_t len);
void vks_llsd_fmt_uuid(VksLlsdFormatter* f, const uint8_t* bytes16);
void vks_llsd_fmt_string(VksLlsdFormatter* f, const uint8_t* p, size_t len);
void vks_llsd_fmt_date(VksLlsdFormatter* f, double seconds);
void vks_llsd_fmt_uri(VksLlsdFormatter* f, const uint8_t* p, size_t len);
void vks_llsd_fmt_binary(VksLlsdFormatter* f, const uint8_t* p, size_t len);
void vks_llsd_fmt_map_begin(VksLlsdFormatter* f, size_t count);
void vks_llsd_fmt_key(VksLlsdFormatter* f, const uint8_t* p, size_t len);
void vks_llsd_fmt_map_end(VksLlsdFormatter* f);
void vks_llsd_fmt_array_begin(VksLlsdFormatter* f, size_t count);
void vks_llsd_fmt_array_end(VksLlsdFormatter* f);
/* Flushes, frees and returns the number of values written (-1 on failure). */
int32_t vks_llsd_formatter_finish(VksLlsdFormatter* f);

/* The notation (kind 0) or XML (kind 1) escape of len bytes, to write. */
void vks_llsd_escape(int32_t kind, const uint8_t* p, size_t len, void* ctx, VksLlsdWrite write);

#ifdef __cplusplus
}
#endif

#endif /* VKSTORM_FFI_H */
