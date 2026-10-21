/**
 * @file vkstorm_ffi.h
 * @brief C ABI exported by the Rust crate indra/rust/vkstorm-ffi.
 *
 * Keep in step with indra/rust/vkstorm-ffi/src/lib.rs (JPEG 2000 and
 * LLSD), indra/rust/vkstorm-ffi/src/volume.rs (the prim mesher),
 * indra/rust/vkstorm-ffi/src/image.rs (PNG, TGA, BMP, DXT),
 * indra/rust/vkstorm-ffi/src/messages.rs (message templates, data packers)
 * and indra/rust/vkstorm-ffi/src/http.rs (the HTTP layer).
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

/* ---- Message templates and data packers (rung three) ------------------- */
/* See indra/rust/vkstorm-ffi/src/messages.rs. Blocks and variables are
 * identified by 64-bit keys: the viewer passes its canonical string
 * pointers. Errors the C++ aborted on come back as codes; the text is in
 * vks_msg_last_error() (thread-local, until the next failing call). */

#define VKS_MSG_OK 0
#define VKS_MSG_ERR_INTERNAL (-1)
#define VKS_MSG_ERR_NO_MESSAGE (-2)
#define VKS_MSG_ERR_NOT_A_BLOCK (-3)
#define VKS_MSG_ERR_SINGLE_REPEATED (-4)
#define VKS_MSG_ERR_MULTIPLE_EXCEEDED (-5)
#define VKS_MSG_ERR_TOO_MANY_BLOCKS (-6)
#define VKS_MSG_ERR_NO_BLOCK (-7)
#define VKS_MSG_ERR_NOT_A_VARIABLE (-8)
#define VKS_MSG_ERR_SIZE_MISMATCH (-9)
#define VKS_MSG_ERR_BUILD (-10)

typedef struct VksMsgTokenizer VksMsgTokenizer;
typedef struct VksMsgDesc VksMsgDesc;
typedef struct VksMsgTemplate VksMsgTemplate;
typedef struct VksMsgBuilder VksMsgBuilder;
typedef struct VksMsgReader VksMsgReader;

const char* vks_msg_last_error(void);

/* LLTemplateTokenizer */
VksMsgTokenizer* vks_msg_tokenizer_new(const uint8_t* text, size_t len);
VksMsgTokenizer* vks_msg_tokenizer_clone(const VksMsgTokenizer* t);
void vks_msg_tokenizer_free(VksMsgTokenizer* t);
int32_t vks_msg_tokenizer_at_eof(const VksMsgTokenizer* t);
uint32_t vks_msg_tokenizer_line(const VksMsgTokenizer* t);
/* 1 with the token (valid until the next call on t), 0 on error. */
int32_t vks_msg_tokenizer_next(VksMsgTokenizer* t, const uint8_t** out, size_t* len);
/* 1 consumed, 0 not the next token, -1 error. */
int32_t vks_msg_tokenizer_want(VksMsgTokenizer* t, const uint8_t* tok, size_t len);
int32_t vks_msg_tokenizer_want_eof(VksMsgTokenizer* t);

/* LLTemplateParser: descriptions the viewer turns into its own objects. */
typedef struct VksMsgInfo
{
    const uint8_t* name;
    size_t name_len;
    uint32_t number;
    int32_t frequency;
    int32_t trust;
    int32_t encoding;
    int32_t deprecation;
    int32_t total_size;
    uint32_t block_count;
} VksMsgInfo;

typedef struct VksMsgBlockInfo
{
    const uint8_t* name;
    size_t name_len;
    int32_t type;
    int32_t number;
    int32_t total_size;
    uint32_t var_count;
} VksMsgBlockInfo;

typedef struct VksMsgVarInfo
{
    const uint8_t* name;
    size_t name_len;
    int32_t type;
    int32_t size;
} VksMsgVarInfo;

int32_t vks_msg_parse_version(VksMsgTokenizer* t, float* out);
/* A description with status 1; null with status 0 when the next token is
 * not "{", null with status -1 on error. */
VksMsgDesc* vks_msg_parse_message(VksMsgTokenizer* t, int32_t* status);
VksMsgDesc* vks_msg_parse_block(VksMsgTokenizer* t, int32_t* status);
VksMsgDesc* vks_msg_parse_variable(VksMsgTokenizer* t, int32_t* status);
void vks_msg_desc_free(VksMsgDesc* d);
int32_t vks_msg_desc_message(const VksMsgDesc* d, VksMsgInfo* out);
int32_t vks_msg_desc_block(const VksMsgDesc* d, uint32_t block, VksMsgBlockInfo* out);
int32_t vks_msg_desc_variable(const VksMsgDesc* d, uint32_t block, uint32_t var, VksMsgVarInfo* out);

/* A template compiled from an LLMessageTemplate; builders and readers that
 * hold it keep it alive after vks_msg_template_free. */
VksMsgTemplate* vks_msg_template_new(const uint8_t* name, size_t name_len, uint64_t key, uint32_t number,
                                     int32_t frequency, int32_t trust, int32_t encoding, int32_t deprecation);
int32_t vks_msg_template_add_block(VksMsgTemplate* t, const uint8_t* name, size_t name_len, uint64_t key, int32_t type, int32_t number);
int32_t vks_msg_template_add_variable(VksMsgTemplate* t, uint32_t block, const uint8_t* name, size_t name_len, uint64_t key, int32_t type, int32_t size);
int32_t vks_msg_template_total_size(const VksMsgTemplate* t, int32_t block);
void vks_msg_template_free(VksMsgTemplate* t);

/* LLTemplateMessageBuilder */
VksMsgBuilder* vks_msg_builder_new(void);
void vks_msg_builder_free(VksMsgBuilder* b);
/* 1 when the message is deprecated (warn), else 0. */
int32_t vks_msg_builder_new_message(VksMsgBuilder* b, const VksMsgTemplate* t);
void vks_msg_builder_clear(VksMsgBuilder* b);
int32_t vks_msg_builder_next_block(VksMsgBuilder* b, uint64_t key);
int32_t vks_msg_builder_remove_last_block(VksMsgBuilder* b);
/* >= 0: flags (1 clamped to 255 bytes, 2 type mismatch); < 0 error. */
int32_t vks_msg_builder_add(VksMsgBuilder* b, uint64_t key, const uint8_t* data, size_t len, int32_t type);
int32_t vks_msg_builder_is_full(const VksMsgBuilder* b, uint64_t key, int32_t has_key);
int32_t vks_msg_builder_message_size(const VksMsgBuilder* b);
int32_t vks_msg_builder_is_built(const VksMsgBuilder* b);
void vks_msg_builder_set_built(VksMsgBuilder* b, int32_t built);
int32_t vks_msg_builder_is_clear(const VksMsgBuilder* b);
/* The packed size, or an error code. */
int64_t vks_msg_builder_build(VksMsgBuilder* b, uint8_t* buffer, size_t size, uint8_t offset);
int32_t vks_msg_builder_block_repeats(const VksMsgBuilder* b, uint32_t block);
const uint8_t* vks_msg_builder_slot(const VksMsgBuilder* b, uint32_t block, uint32_t repeat, uint32_t var, size_t* len, int32_t* data_size);

/* LLTemplateMessageReader */
VksMsgReader* vks_msg_reader_new(void);
void vks_msg_reader_free(VksMsgReader* r);
/* The message number (>= 0), -1 empty, -2 too short. */
int64_t vks_msg_decode_number(const uint8_t* buffer, size_t size);
void vks_msg_reader_set_message(VksMsgReader* r, const VksMsgTemplate* t, int32_t receive_size);
/* 1 when the message had blocks, 0 when none; events counts the reads
 * past the end (details through vks_msg_reader_event). */
int32_t vks_msg_reader_decode(VksMsgReader* r, const uint8_t* buffer, size_t size, int32_t* events);
int32_t vks_msg_reader_event(const VksMsgReader* r, uint32_t i, int32_t* position, int32_t* wanted);
void vks_msg_reader_clear(VksMsgReader* r);
int32_t vks_msg_reader_message_size(const VksMsgReader* r);
/* 0 ok, 1 cut to max_size, or an error code; actual gets the size. */
int32_t vks_msg_reader_get(const VksMsgReader* r, uint64_t block, int32_t blocknum, uint64_t var, uint8_t* dst, int32_t size, int32_t max_size, int32_t* actual);
int32_t vks_msg_reader_number_of_blocks(const VksMsgReader* r, uint64_t block);
int32_t vks_msg_reader_size(const VksMsgReader* r, uint64_t block, int32_t blocknum, uint64_t var);
int32_t vks_msg_reader_block_repeats(const VksMsgReader* r, uint32_t block);
const uint8_t* vks_msg_reader_slot(const VksMsgReader* r, uint32_t block, uint32_t repeat, uint32_t var, size_t* len);

/* Zero-coding */
/* The coded length in out (flag set), 0 when coding would not shrink the
 * packet, -1 on bad arguments. out needs len + 1 bytes. */
int32_t vks_msg_zero_code(const uint8_t* packet, size_t len, uint8_t* out, size_t out_len);
int32_t vks_msg_zero_code_gain(const uint8_t* packet, size_t len);
/* The expanded length in out (flag cleared); -1/-2 when the packet would
 * pass the end of out. overruns counts runs written from the start. */
int32_t vks_msg_zero_code_expand(const uint8_t* packet, size_t len, uint8_t* out, size_t out_len, int32_t* overruns);

/* LLDataPacker: the buffer state the C++ object keeps, per call. */
typedef struct VksDpBuffer
{
    uint8_t* data;
    int32_t size;
    int32_t cur;
    int32_t write_enabled;
    int32_t include_names;
} VksDpBuffer;

#define VKS_DP_STRING 0
#define VKS_DP_BINARY 1
#define VKS_DP_BINARY_FIXED 2
#define VKS_DP_INT 3
#define VKS_DP_UINT 4
#define VKS_DP_FLOATS 5
#define VKS_DP_BYTES 6
#define VKS_DP_UUID 7
#define VKS_DP_FLOATS_SHORT 8 /* %f cut to 19 characters, the stream file form */

int32_t vks_dp_bin_pack(VksDpBuffer* s, const uint8_t* v, size_t len);
int32_t vks_dp_bin_unpack(VksDpBuffer* s, uint8_t* dst, size_t len);
int32_t vks_dp_bin_pack_s16(VksDpBuffer* s, int16_t v);
int32_t vks_dp_bin_unpack_s16(VksDpBuffer* s, int16_t* out);
int32_t vks_dp_bin_pack_string(VksDpBuffer* s, const uint8_t* v, size_t len);
int32_t vks_dp_bin_unpack_string(VksDpBuffer* s, const uint8_t** out, size_t* len);
int32_t vks_dp_bin_pack_binary(VksDpBuffer* s, const uint8_t* v, size_t len);
int32_t vks_dp_bin_unpack_binary(VksDpBuffer* s, const uint8_t** out, size_t* len);
int32_t vks_dp_ascii_pack(VksDpBuffer* s, int32_t kind, const uint8_t* name, size_t name_len,
                          const int64_t* ints, size_t nints, const float* floats, size_t nfloats,
                          const uint8_t* data, size_t ndata, int32_t* truncated);
int32_t vks_dp_ascii_next_value(VksDpBuffer* s, const uint8_t* name, size_t name_len, uint8_t* out, size_t cap, size_t* len);
int32_t vks_dp_ascii_parse(int32_t kind, const uint8_t* text, size_t len, size_t count,
                           int64_t* ints, float* floats, uint8_t* data, size_t data_cap, int32_t* size, size_t* ndata);
size_t vks_dp_ascii_format(int32_t kind, const int64_t* ints, size_t nints, const float* floats, size_t nfloats,
                           const uint8_t* data, size_t ndata, uint8_t* out, size_t cap);
void vks_dp_ascii_split(const uint8_t* line, size_t len, const uint8_t** key, size_t* key_len, const uint8_t** value, size_t* value_len);
uint32_t vks_dp_fixed_pack(float value, int32_t is_signed, uint32_t int_bits, uint32_t frac_bits, int32_t* total_bits);
float vks_dp_fixed_unpack(uint32_t fixed, int32_t is_signed, uint32_t int_bits, uint32_t frac_bits);
int32_t vks_dp_fixed_total_bits(int32_t is_signed, uint32_t int_bits, uint32_t frac_bits);

/* ---- HTTP layer (rung four) --------------------------------------------- */

/* The service of LLCore::HttpRequest: request queue, service thread, policy
 * classes, retry queue and the libcurl transport live in the crate
 * (vkstorm-http). One service per process. Handles are a 64-bit counter,
 * never recycled; the C++ casts them to HttpHandle. */

/* What the SSL context hook receives for a request whose connection is
 * being set up, and where its verification callback writes its decision
 * (a status type and code; code 0 is success). Lives as long as the
 * request is active. */
typedef struct VksHttpSslInfo
{
    uint64_t    handle;
    void*       user;
    const char* url;         /* NUL-terminated */
    int32_t     verify_peer;
    uint16_t    result_type;
    int16_t     result_code;
} VksHttpSslInfo;

/* The host's facilities. Any function may be null. Levels: 0 debug, 1 info,
 * 2 warning. Stat kinds: 0 bytes received, 1 bytes sent, 2 result code. */
typedef struct VksHttpHooks
{
    void* ctx;
    void (*log)(void* ctx, int32_t level, const char* text, size_t len);
    void (*stat)(void* ctx, int32_t kind, uint64_t value);
    void (*apply_proxy)(void* ctx, void* curl);                       /* LLProxy::applyProxySettings */
    void (*ssl_ctx)(void* ctx, void* ssl_ctx, VksHttpSslInfo* info);  /* CURLOPT_SSL_CTX_FUNCTION body */
    void (*thread_start)(void* ctx);
    void (*thread_stop)(void* ctx);
    void (*release_user)(void* ctx, void* user);                      /* user data never delivered */
} VksHttpHooks;

/* LLCore::HttpStatus by value. */
typedef struct VksHttpStatus
{
    uint16_t type;
    int16_t  code;
} VksHttpStatus;

/* LLCore::HttpOptions by value. */
typedef struct VksHttpOptions
{
    int32_t  want_headers;
    int32_t  trace;
    uint32_t timeout;
    uint32_t transfer_timeout;
    uint32_t retries;
    uint64_t min_backoff;
    uint64_t max_backoff;
    int32_t  use_retry_after;
    int32_t  follow_redirects;
    int32_t  verify_peer;
    int32_t  verify_host;
    int32_t  dns_cache_timeout;
    int32_t  headers_only;
    int64_t  last_modified;
} VksHttpOptions;

typedef struct VksHttpHeader
{
    const char* name;
    size_t      name_len;
    const char* value;
    size_t      value_len;
} VksHttpHeader;

/* method: 0 GET, 1 POST, 2 PUT, 3 DELETE, 4 PATCH, 5 COPY, 6 MOVE. */
typedef struct VksHttpRequestDesc
{
    uint32_t              policy;
    int32_t               method;
    const char*           url;
    size_t                url_len;
    const uint8_t*        body;
    size_t                body_len;
    int32_t               has_body;
    uint64_t              offset;
    uint64_t              length;
    const VksHttpHeader*  headers;
    size_t                header_count;
    const VksHttpOptions* options;   /* may be null */
} VksHttpRequestDesc;

typedef struct VksHttpReplyQueue VksHttpReplyQueue;
typedef struct VksHttpReply VksHttpReply;

/* Service life cycle. States: -1 not initialised, 0 initialised, 1 running,
 * 2 stopped. */
int32_t  vks_http_service_create(const VksHttpHooks* hooks, const char* user_agent);
void     vks_http_service_destroy(void);
int32_t  vks_http_service_state(void);
void     vks_http_service_start_thread(void);

/* Policy classes and the static option interface (before the thread runs).
 * opt is HttpRequest::EPolicyOption; pclass a class id or GLOBAL_POLICY_ID. */
uint32_t      vks_http_policy_class_create(void);
VksHttpStatus vks_http_policy_set_long(int32_t opt, uint32_t pclass, int64_t value, int64_t* ret);
VksHttpStatus vks_http_policy_set_string(int32_t opt, uint32_t pclass, const char* value, size_t len);
VksHttpStatus vks_http_policy_set_callback(int32_t opt, uint32_t pclass, int32_t set);
VksHttpStatus vks_http_policy_get_long(int32_t opt, uint32_t pclass, int64_t* ret);
VksHttpStatus vks_http_policy_get_string(int32_t opt, uint32_t pclass, char* buf, size_t cap, size_t* len);

/* Reply queues: one per HttpRequest instance. */
VksHttpReplyQueue* vks_http_reply_queue_new(void);
void               vks_http_reply_queue_free(VksHttpReplyQueue* q);

/* Operations. Each returns the handle, or 0 with *status set. `user` is
 * handed back with the reply (or through release_user). */
uint64_t vks_http_request_submit(VksHttpReplyQueue* q, const VksHttpRequestDesc* desc, void* user, VksHttpStatus* status);
uint64_t vks_http_request_cancel(VksHttpReplyQueue* q, uint64_t target, void* user, VksHttpStatus* status);
uint64_t vks_http_request_noop(VksHttpReplyQueue* q, void* user, VksHttpStatus* status);
uint64_t vks_http_request_stop(VksHttpReplyQueue* q, void* user, VksHttpStatus* status);
uint64_t vks_http_request_spin(VksHttpReplyQueue* q, int32_t mode, VksHttpStatus* status);
uint64_t vks_http_request_set_long(VksHttpReplyQueue* q, int32_t opt, uint32_t pclass, int64_t value, void* user, VksHttpStatus* status);
uint64_t vks_http_request_set_string(VksHttpReplyQueue* q, int32_t opt, uint32_t pclass, const char* value, size_t len, void* user, VksHttpStatus* status);

/* Replies. Kinds: 0 request, 1 cancel, 2 option set/get, 3 stop, 4 no-op.
 * Pointers returned by the accessors are valid until the reply is freed;
 * strings are not NUL-terminated. */
VksHttpReply* vks_http_reply_fetch(VksHttpReplyQueue* q);
void          vks_http_reply_free(VksHttpReply* r);
uint64_t      vks_http_reply_handle(const VksHttpReply* r);
void*         vks_http_reply_user(const VksHttpReply* r);
int32_t       vks_http_reply_kind(const VksHttpReply* r);
VksHttpStatus vks_http_reply_status(const VksHttpReply* r);
const uint8_t* vks_http_reply_body(const VksHttpReply* r, size_t* len);
int32_t       vks_http_reply_has_headers(const VksHttpReply* r);
size_t        vks_http_reply_header_count(const VksHttpReply* r);
int32_t       vks_http_reply_header(const VksHttpReply* r, size_t i, VksHttpHeader* out);
const char*   vks_http_reply_content_type(const VksHttpReply* r, size_t* len);
int32_t       vks_http_reply_range(const VksHttpReply* r, uint64_t* offset, uint64_t* length, uint64_t* full);
uint32_t      vks_http_reply_retries(const VksHttpReply* r);
uint32_t      vks_http_reply_retries_503(const VksHttpReply* r);
const char*   vks_http_reply_url(const VksHttpReply* r, size_t* len);
const char*   vks_http_reply_method(const VksHttpReply* r, size_t* len);
void          vks_http_reply_stats(const VksHttpReply* r, double* size_download, double* total_time, double* speed_download);
int64_t       vks_http_reply_long(const VksHttpReply* r);
const char*   vks_http_reply_string(const VksHttpReply* r, size_t* len);

/* HttpStatus::isRetryable, and curl_version() of the linked libcurl. */
int32_t     vks_http_status_is_retryable(uint16_t type, int16_t code);
const char* vks_http_curl_version(void);

#ifdef __cplusplus
}
#endif

#endif /* VKSTORM_FFI_H */
