/**
 * @file vkstorm_ffi.h
 * @brief C ABI exported by the Rust crate indra/rust/vkstorm-ffi.
 *
 * Keep in step with indra/rust/vkstorm-ffi/src/lib.rs (JPEG 2000 and
 * LLSD), indra/rust/vkstorm-ffi/src/volume.rs (the prim mesher) and
 * indra/rust/vkstorm-ffi/src/image.rs (PNG, TGA, BMP, DXT).
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

/* ---- Parametric prim mesher ---------------------------------------------
 *
 * Vectors ("float*" below) are LLVector4a: four floats, 16-byte aligned.
 * Profile and path generation return pointers into per-thread scratch
 * storage, valid until the next generate call on the same thread; the
 * sweep, the face builders and the tangents write into the caller's
 * buffers. Every function returns -1 on bad arguments or an internal
 * failure (panics are caught at the boundary).
 */

/* LLProfileParams. */
typedef struct VksProfileParams
{
    uint8_t curve;
    float begin;
    float end;
    float hollow;
} VksProfileParams;

/* LLPathParams. */
typedef struct VksPathParams
{
    uint8_t curve;
    float begin;
    float end;
    float scale_x;
    float scale_y;
    float shear_x;
    float shear_y;
    float twist_begin;
    float twist_end;
    float radius_offset;
    float taper_x;
    float taper_y;
    float revolutions;
    float skew;
} VksPathParams;

/* LLProfile::Face, same layout (16 bytes). */
typedef struct VksProfileFace
{
    int32_t index;
    int32_t count;
    float scale_u;
    uint8_t cap;
    uint8_t flat;
    uint16_t face_id;
} VksProfileFace;

/* LLPath::PathPt, same layout (112 bytes; the buffers are 16-byte aligned). */
typedef struct VksPathPt
{
    float rot[4][4];
    float pos[4];
    float scale[4];
    float tex_t;
    float pad[3];
} VksPathPt;

typedef struct VksProfileOut
{
    const float* points;          /* point_count vectors */
    uint32_t point_count;
    const VksProfileFace* faces;
    uint32_t face_count;
    int32_t total;
    int32_t total_out;
    uint8_t open;
    uint8_t concave;
} VksProfileOut;

typedef struct VksPathOut
{
    const VksPathPt* points;
    uint32_t count;
    uint8_t open;
    float step;
} VksPathOut;

/* LLProfile::generate: 1 generated, 0 invalid cut (begin > end - 0.01; empty
 * profile), -1 failure. */
int32_t vks_volume_profile_generate(const VksProfileParams* params, int32_t path_open, float detail,
                                    int32_t split, int32_t is_sculpted, int32_t sculpt_size,
                                    VksProfileOut* out);
/* LLPath::generate: 1 generated, -1 failure. */
int32_t vks_volume_path_generate(const VksPathParams* params, float detail, int32_t split,
                                 int32_t is_sculpted, int32_t sculpt_size, VksPathOut* out);
/* LLProfile::getNumPoints and LLPath::getNumPoints (LOD triangle estimates). */
int32_t vks_volume_profile_num_points(const VksProfileParams* params, int32_t path_open, float detail,
                                      int32_t split, int32_t is_sculpted, int32_t sculpt_size);
int32_t vks_volume_path_num_points(const VksPathParams* params, float detail);

/* The mesh sweep of LLVolume::generate: path_count * profile_count vectors
 * into dst (path-major). 1 on success. */
int32_t vks_volume_sweep(const VksPathPt* path, uint32_t path_count, const float* profile,
                         uint32_t profile_count, float* dst);

/* What a face build reads: the volume's mesh, profile and path, the face
 * fields LLVolume::createVolumeFaces set, and the parameters consulted. */
typedef struct VksFaceParams
{
    const float* mesh;
    uint32_t mesh_count;
    const float* profile;
    uint32_t profile_count;
    int32_t profile_total;
    uint8_t profile_open;
    const VksPathPt* path;
    uint32_t path_count;
    uint8_t path_open;
    int32_t id;
    uint32_t type_mask;
    int32_t begin_s;
    int32_t num_s;
    int32_t begin_t;
    int32_t num_t;
    uint8_t profile_curve;
    uint8_t path_curve;
    uint8_t sculpt_type;
    float path_begin;
    float path_end;
} VksFaceParams;

/* The LLVolumeFace buffers; num_vertices/num_indices are the allocated
 * counts (mNumVertices, mNumIndices). The texture coordinate buffer must
 * have the even-count padding slot resizeVertices allocates. */
typedef struct VksFaceBuffers
{
    float* positions;
    float* normals;
    float* texcoords;   /* num_vertices pairs (+ padding) */
    uint16_t* indices;
    int32_t num_vertices;
    int32_t num_indices;
    uint8_t write_indices; /* !partial_build */
} VksFaceBuffers;

typedef struct VksFaceResult
{
    float extents_min[4];
    float extents_max[4];
    float center[4];
    float tc_min[2];
    float tc_max[2];
    uint8_t tc_extents_set; /* sides set the texture coordinate extents, caps do not */
} VksFaceResult;

enum
{
    VKS_FACE_SIDE = 0,
    VKS_FACE_CAP = 1,
    VKS_FACE_UNCUT_CUBE_CAP = 2
};

/* Which builder the face takes (VKS_FACE_*) and the vertex and index counts
 * it produces; -1 on bad arguments. */
int32_t vks_volume_face_sizes(const VksFaceParams* p, int32_t* num_vertices, int32_t* num_indices);
/* LLVolumeFace::create into the buffers: 1 built, 0 degenerate inputs
 * (nothing written), -1 failure. */
int32_t vks_volume_face_build(const VksFaceParams* p, const VksFaceBuffers* b, VksFaceResult* out);

/* LLCalculateTangentArray into tangents (handedness in w); with
 * normalize_normals the normals are normalised in place afterwards as
 * LLVolumeFace::createTangents does (otherwise they are only read). */
int32_t vks_volume_tangents(int32_t num_vertices, const float* positions, float* normals,
                            const float* texcoords, int32_t num_indices, const uint16_t* indices,
                            float* tangents, int32_t normalize_normals);

/* ---- Raster codecs: PNG, TGA, BMP, DXT (rung two) ---------------------- */

typedef struct VksImageHeader
{
    uint32_t width;
    uint32_t height;
    uint32_t components;
} VksImageHeader;

typedef struct VksRaster VksRaster; /* a decoded image, rows bottom-up */
typedef struct VksBytes VksBytes;   /* an encoded file */

/* The message of the last failed vks_png_*, vks_tga_*, vks_bmp_* or vks_dxt_*
 * call on this thread (the text the C++ codec passed to setLastError),
 * NUL-terminated, valid until the next failing call on the thread. */
const char* vks_image_last_error(void);

uint32_t vks_raster_width(const VksRaster* r);
uint32_t vks_raster_height(const VksRaster* r);
uint32_t vks_raster_components(const VksRaster* r);
/* width*height*components bytes, bottom-up, valid until freed. */
const uint8_t* vks_raster_data(const VksRaster* r);
size_t vks_raster_len(const VksRaster* r);
void vks_raster_free(VksRaster* r);

const uint8_t* vks_bytes_data(const VksBytes* b);
size_t vks_bytes_len(const VksBytes* b);
void vks_bytes_free(VksBytes* b);

/* PNG: header read (1 on success), decode (3 or 4 components, as libpng with
 * the viewer's transformations produced), encode of 1 to 4 components. */
int32_t vks_png_header(const uint8_t* data, size_t len, VksImageHeader* out);
VksRaster* vks_png_decode(const uint8_t* data, size_t len);
VksBytes* vks_png_encode(const uint8_t* data, size_t len, uint32_t width, uint32_t height, uint32_t components);

/* TGA */
typedef struct VksTgaHeader
{
    uint32_t width;
    uint32_t height;
    uint32_t components;
    uint32_t image_type;
    uint32_t origin_top;
    uint32_t origin_right;
} VksTgaHeader;

int32_t vks_tga_header(const uint8_t* data, size_t len, VksTgaHeader* out);
VksRaster* vks_tga_decode(const uint8_t* data, size_t len);
/* LLImageTGA::decodeAndProcess: monochrome run-length through the avatar
 * alpha-gradient function. */
VksRaster* vks_tga_decode_and_process(const uint8_t* data, size_t len, float domain, float weight);
VksBytes* vks_tga_encode(const uint8_t* data, size_t len, uint32_t width, uint32_t height, uint32_t components);

/* BMP: the header's components are what LLImageBMP reports (1 for 8-bit
 * files); decodes always have 3. */
int32_t vks_bmp_header(const uint8_t* data, size_t len, VksImageHeader* out);
VksRaster* vks_bmp_decode(const uint8_t* data, size_t len);
VksBytes* vks_bmp_encode(const uint8_t* data, size_t len, uint32_t width, uint32_t height, uint32_t components,
                         int32_t pels_per_meter);

/* DXT: the viewer's mipped container. format is LLImageDXT::EFileFormat. */
typedef struct VksDxtHeader
{
    uint32_t width;
    uint32_t height;
    uint32_t components;
    int32_t format;
    int32_t header_size;
    int32_t discard_level;
} VksDxtHeader;

int32_t vks_dxt_header(const uint8_t* data, size_t len, VksDxtHeader* out);
VksRaster* vks_dxt_decode(const uint8_t* data, size_t len, const VksDxtHeader* header);
VksRaster* vks_dxt_mip_data(const uint8_t* data, size_t len, const VksDxtHeader* header, int32_t discard);
VksBytes* vks_dxt_encode(const uint8_t* data, size_t len, uint32_t width, uint32_t height, uint32_t components);
/* Null when there is nothing to convert or on failure. */
VksBytes* vks_dxt_convert_to_dxr(const uint8_t* data, size_t len, const VksDxtHeader* header);
int32_t vks_dxt_data_size(const VksDxtHeader* header, int32_t discard);   /* -1 on failure */
int32_t vks_dxt_mip_offset(int32_t format, int32_t header_size, int32_t width, int32_t height, int32_t discard); /* -1 on failure */
int32_t vks_dxt_format_bits(int32_t format);                                /* 0 if unknown */
int32_t vks_dxt_format_bytes(int32_t format, int32_t width, int32_t height); /* 0 if unknown */
int32_t vks_dxt_format_components(int32_t format);                          /* 0 if unknown */
int32_t vks_dxt_format_from_fourcc(int32_t fourcc);
int32_t vks_dxt_fourcc(int32_t format);
int32_t vks_dxt_calc_num_mips(int32_t width, int32_t height);
void vks_dxt_calc_discard_width_height(int32_t discard, int32_t format, int32_t* width, int32_t* height);
void vks_dxt_check_min_width_height(int32_t format, int32_t* width, int32_t* height);

#ifdef __cplusplus
}
#endif

#endif /* VKSTORM_FFI_H */
