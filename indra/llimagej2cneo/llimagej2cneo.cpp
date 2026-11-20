// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The viewer's J2C decoder and encoder on neo-j2k (https://github.com/anne-skydancer/neo-j2k, BSD-2-Clause), for
// builds with USE_NEOJ2K. It does what llimagej2coj does, with the same rules (the SL decoder limits, the reduce
// factor, the decode guard, the comment scan), on neo-j2k's memory API:
//
// - One decoder for each thread that decodes (the ImageDecode pool, the main thread for local files and the bake
//   integrity checks), created on first use and destroyed at thread exit. It keeps its working memory for the
//   next image, and it reads each header once: getMetadata and the decode that follows it on the same bytes share
//   the parse.
// - The decode writes the samples straight into the LLImageRaw (8 bits, interleaved, bottom-up): no int32 planes
//   and no copy loop.
// - A pool thread decoding while the pool is idle lends neo-j2k the pool as its executor (see lendPool below);
//   every other decode runs on its own thread.
// - Encoding makes a codec for each call: uploads and bakes are rare, and the encoder keeps nothing between images.
//
// What changes in the pixels is what neo-j2k's recorded departures say (doc/departures.md in neo-j2k): whole
// files decode bit for bit as OpenJPEG's, and cut downloads decode as the fork's 1a hotfix does.

#include "linden_common.h"
#include "llimagej2cneo.h"
#include "llj2creduce.h"        // llimagej2coj/: header-only, shared with it
#include "llj2cdecodeguard.h"
#include "llimagefpstate.h"
#include "llimageworker.h"

#include <neoj2k/neoj2k.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// Factory function: see declaration in llimagej2c.cpp
LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl()
{
    return new LLImageJ2CNeo();
}

// What the texture cache keys on (lltexturecache.cpp reads it during static initialisation: pure, at most 31
// characters). It names the viewer's decoder output, not neo-j2k's release (owner decision D13: neo-j2k's engine
// string changes with every release, which would purge every user's cache). Bump the number when the decoded
// pixels can change.
std::string LLImageJ2CNeo::getEngineInfo() const
{
    return "neo-j2k output 1";
}

namespace
{
    static_assert(std::is_same<LLImageDecodeThread::job_fn, nj2k_job_fn>::value, "the pool's job type is neo-j2k's");

    // The first message neo-j2k reports for a refusal: it names the cause (the SL limits, for one) before the
    // generic failure that follows it. Logged by the caller when the header or the decode fails.
    void errorCallback(const char* msg, void* user_data)
    {
        std::string* last_error = static_cast<std::string*>(user_data);
        if (last_error && msg && last_error->empty())
        {
            *last_error = msg;
            while (!last_error->empty() && (last_error->back() == '\n' || last_error->back() == '\r'))
            {
                last_error->pop_back();
            }
        }
    }

    // Retained between decodes: its working arenas, Tier-1 and wavelet buffers and the stream's bytes. Pool
    // threads keep theirs warm for the small textures that make most decodes.
    constexpr size_t RETAINED_BYTES = 4u << 20;

    // The decoder of one thread. It starts with the SL limits, non-strict mode (a cut download decodes what it
    // holds) and the reduce factor 0, which every request starts from and ends on.
    class ThreadDecoder
    {
    public:
        ThreadDecoder()
        {
            codec = nj2k_create_decompress(NJ2K_CODEC_J2K);
            if (!codec)
            {
                return;
            }
            nj2k_set_error_handler(codec, errorCallback, &last_error);
            nj2k_dparameters_t parameters;
            nj2k_set_default_decoder_parameters(&parameters);
            nj2k_decoder_limits_t limits;
            usable = nj2k_setup_decoder(codec, &parameters)
                     && nj2k_decoder_limits_init(&limits, NJ2K_DECODER_LIMITS_SL_TEXTURE)
                     && nj2k_decoder_set_limits(codec, &limits)
                     && nj2k_decoder_set_strict_mode(codec, NJ2K_FALSE)
                     && nj2k_decoder_set_retained_bytes(codec, RETAINED_BYTES);
            if (!usable)
            {
                // a decoder without the limits would take any header at its word: refuse every decode
                LL_WARNS("Texture") << "J2C: the neo-j2k decoder could not be set up, refusing to decode" << LL_ENDL;
            }
        }
        ~ThreadDecoder()
        {
            if (codec)
            {
                nj2k_destroy_codec(codec);
            }
        }
        ThreadDecoder(const ThreadDecoder&) = delete;
        ThreadDecoder& operator=(const ThreadDecoder&) = delete;

        // The header of `data`, at the reduce factor in effect (0 between requests).
        bool readHeader(const U8* data, size_t size, nj2k_image_t& image)
        {
            last_error.clear();
            return usable && nj2k_read_header_memory(codec, data, size, &image);
        }

        // Every request ends with the factor back at 0, so that the next header, another image's, is read at
        // the full size whatever this one asked for. It is always accepted: 0 is below every resolution count.
        void resetReduce()
        {
            nj2k_set_decoded_resolution_factor(codec, 0);
        }

        nj2k_codec_t* codec = nullptr;
        bool usable = false;
        std::string last_error; // the first message of the last header read or decode
    };

    // One per thread that decodes; destroyed at thread exit. The ImageDecode pool joins its threads at
    // shutdown (LLImageDecodeThread::shutdown), so no worker's decoder outlives the process's clean exit.
    ThreadDecoder& threadDecoder()
    {
        thread_local ThreadDecoder decoder;
        return decoder;
    }

    // neo-j2k's executor on the viewer's ImageDecode pool: every job is run by the calling pool thread or by an
    // idle worker, and the calling thread runs what no worker has taken, so the loop cannot wait on a busy pool.
    void poolParallelFor(void* user, NJ2K_UINT32 count, nj2k_job_fn job, void* job_ctx)
    {
        static_cast<LLImageDecodeThread*>(user)->parallelFor(count, job, job_ctx);
    }

    // Lends neo-j2k the pool for one decode, when the decode runs on a pool thread and the pool is idle (fewer
    // decode requests waiting than workers). Otherwise the decode runs on its own thread, as it should in a busy
    // pool, where splitting it would cost throughput. Takes the executor back at the end of the scope.
    class PoolLoan
    {
    public:
        explicit PoolLoan(nj2k_codec_t* codec_in) : codec(codec_in)
        {
            LLImageDecodeThread* pool = LLImageDecodeThread::getCurrent();
            if (pool && pool->isIdle() && pool->getWorkerCount() > 1)
            {
                nj2k_executor_t executor = {};
                executor.struct_size = sizeof(executor);
                executor.concurrency = std::min<U32>(pool->getWorkerCount(), 64);
                executor.parallel_for = poolParallelFor;
                executor.user = pool;
                lent = nj2k_decoder_set_executor(codec, &executor);
            }
        }
        ~PoolLoan()
        {
            if (lent)
            {
                nj2k_decoder_set_executor(codec, nullptr);
            }
        }
        PoolLoan(const PoolLoan&) = delete;
        PoolLoan& operator=(const PoolLoan&) = delete;

    private:
        nj2k_codec_t* codec;
        bool lent = false;
    };

    // Ends a request with the decoder's factor at 0.
    struct ReduceReset
    {
        explicit ReduceReset(ThreadDecoder& d) : decoder(d) {}
        ~ReduceReset() { decoder.resetReduce(); }
        ThreadDecoder& decoder;
    };

    /**
     * Estimates the number of layers necessary depending on the image surface (w x h)
     */
    U32 estimate_num_layers(U32 surface)
    {
        if      (surface <= 1024)    return 2;  // Tiny (<=32x32)
        else if (surface <= 16384)   return 3;  // Small (<=128x128)
        else if (surface <= 262144)  return 4;  // Medium (<=512x512)
        else if (surface <= 1048576) return 5;  // Up to ~1MP
        else                         return 6;  // Up to ~1.5-2MP
    }

    /**
     * Sets the parameters.tcp_rates according to the number of layers and a last tcp_rate value (which equals to the final compression ratio).
     *
     * Example for 6 layers: the rates are 8, 32, 128, 512, 2048 and 8192, the last layer being the lowest quality.
     */
    void set_tcp_rates(nj2k_cparameters_t* parameters, U32 num_layers = 1, F32 last_tcp_rate = LAST_TCP_RATE)
    {
        parameters->tcp_numlayers = num_layers;

        for (int i = num_layers - 1; i >= 0; i--)
        {
            parameters->tcp_rates[num_layers - 1 - i] = last_tcp_rate * static_cast<F32>(1 << (i << 1));
        }
    }

    // The output stream of one encode: the codestream, in memory.
    struct EncodeSink
    {
        std::vector<U8> bytes;
    };

    size_t sinkWrite(void* buffer, size_t count, void* user_data)
    {
        EncodeSink* sink = static_cast<EncodeSink*>(user_data);
        const size_t MAX_BUFFER_SIZE = 512u * 1024 * 1024; // 512 MB
        if (!buffer || count > MAX_BUFFER_SIZE || sink->bytes.size() > MAX_BUFFER_SIZE - count)
        {
            return 0;
        }
        const U8* src = static_cast<const U8*>(buffer);
        sink->bytes.insert(sink->bytes.end(), src, src + count);
        return count;
    }

    struct CodecDeleter  { void operator()(nj2k_codec_t* p) const { if (p) nj2k_destroy_codec(p); } };
    struct ImageDeleter  { void operator()(nj2k_image_t* p) const { if (p) nj2k_image_destroy(p); } };
    struct StreamDeleter { void operator()(nj2k_stream_t* p) const { if (p) nj2k_stream_destroy(p); } };
}

LLImageJ2CNeo::LLImageJ2CNeo()
    : LLImageJ2CImpl()
{
}

LLImageJ2CNeo::~LLImageJ2CNeo()
{
}

bool LLImageJ2CNeo::initDecode(LLImageJ2C &base, LLImageRaw &raw_image, int discard_level, int* region)
{
    base.mDiscardLevel = discard_level;
    return false;
}

bool LLImageJ2CNeo::initEncode(LLImageJ2C &base, LLImageRaw &raw_image, int blocks_size, int precincts_size, int levels)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // No specific implementation for this method
    return false;
}

bool LLImageJ2CNeo::decodeImpl(LLImageJ2C &base, LLImageRaw &raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count)
{
    LLImageDataLock lockIn(&base);
    LLImageDataLock lockOut(&raw_image);

    // <FS:Techwolf Lupindo> texture comment metadata reader
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;    // <FS:Beq> instrument image decodes
    U8* c_data = base.getData();
    S32 c_size = base.getDataSize();
    S32 position = 0;

    while (position < 1024 && position < (c_size - 7)) // the comment field should be in the first 1024 bytes.
    {
        if (c_data[position] == 0xff && c_data[position + 1] == 0x64)
        {
            U8 high_byte = c_data[position + 2];
            U8 low_byte = c_data[position + 3];
            S32 c_length = (high_byte * 256) + low_byte; // This size also counts the markers, 00 01 and itself
            if (c_length > 200) // sanity check
            {
                // While comments can be very long, anything longer then 200 is suspect.
                break;
            }

            if (c_length < 4)
            {
                // too short to hold the length field and the 00 01 marker; c_length - 4 below would wrap.
                break;
            }

            if (position + 2 + c_length > c_size)
            {
                // comment extends past end of data, corruption, or all data not retrived yet.
                break;
            }

            // if the comment block does not end at the end of data, check to see if the next
            // block starts with 0xFF
            if (position + 2 + c_length < c_size && c_data[position + 2 + c_length] != 0xff)
            {
                // invalied comment block
                break;
            }

            // extract the comment minus the markers, 00 01
            raw_image.mComment.assign((char*)(c_data + position + 6), c_length - 4);
            break;
        }
        position++;
    }
    // </FS:Techwolf Lupindo>

    LLImageFPScope fpScope; // nearest even, no flush-to-zero, whichever thread this is
    ThreadDecoder& decoder = threadDecoder();
    ReduceReset reduceReset(decoder);

    S32 data_size = base.getDataSize();
    S32 max_bytes = (base.getMaxBytes() ? base.getMaxBytes() : data_size);
    const U8* data = base.getData();

    // The header, from the parse getMetadata made of the same bytes when this thread made it.
    nj2k_image_t image = {};
    bool decoded = decoder.readHeader(data, (size_t)max_bytes, image);
    S32 image_channels = decoded ? (S32)image.numcomps : 0;

    // Never ask for a coarser level than the codestream has (llj2creduce.h): the decoder refuses a reduce
    // factor at or above its resolutions, so a 16 px texture (5 resolutions) failed its first decode (discard
    // 5) and was marked missing. It decodes at its coarsest level now, which setDiscardLevel(f) below reports,
    // when that level is no larger than the requested level of a MAX_IMAGE_SIZE image; every other request (a
    // crafted large codestream with few resolutions among them) is passed on unchanged.
    const S8 reduce = (S8)LLJ2CReduce::reduceFactor(base.mDiscardLevel, base.mNumResolutions, base.getWidth(),
                                                    base.getHeight(), MAX_IMAGE_SIZE);

    if (decoded)
    {
        // Refuse a header that would make the decode allocate gigabytes (llj2cdecodeguard.h), at the size AFTER
        // the discard (reduce) factor, which the limits apply to. The SL profile set on every decoder is the real
        // bound (2048 px per decoded side, 256 MiB, 8 components, checked from the headers by neo-j2k itself);
        // this guard is a second check behind it.
        const U32 ref_w = (image.x1 > image.x0) ? (image.x1 - image.x0) : 0;
        const U32 ref_h = (image.y1 > image.y0) ? (image.y1 - image.y0) : 0;
        if (!LLJ2CDecodeGuard::decodeAllowed(ref_w, ref_h, image.numcomps, reduce, MAX_IMAGE_COMPONENTS))
        {
            LL_WARNS("Texture") << "J2C header declares " << ref_w << "x" << ref_h << " with " << image.numcomps
                                << " components, at discard level " << (U32)reduce << ", refusing to decode"
                                << LL_ENDL;
            decoded = false;
        }
    }
    if (decoded)
    {
        // The factor goes on the held header (checked against it: refused at or above its resolutions), then the
        // header is read again, from the same bytes, for the components at the reduced size. The second read finds
        // the header held and parses nothing.
        decoded = nj2k_set_decoded_resolution_factor(decoder.codec, (NJ2K_UINT32)reduce)
                  && decoder.readHeader(data, (size_t)max_bytes, image)
                  && nj2k_get_decoded_dimensions(decoder.codec, &image);
        if (!decoded)
        {
            LL_DEBUGS("Texture") << "J2C decode: the reduce factor " << (U32)reduce << " was refused ("
                                 << (decoder.last_error.empty() ? "no neo-j2k message" : decoder.last_error.c_str())
                                 << ")" << LL_ENDL;
        }
    }

    // set correct channel count early so failed decodes don't miss it...
    S32 channels = image_channels - first_channel;
    channels = llmin(channels, max_channel_count);

    // Component buffers are written at the decoded size: ceil(width/2^factor) by ceil(height/2^factor), the same
    // for every component the viewer takes. The geometry is checked before anything is sized from it: what the
    // output needs (the raw image takes U16 dimensions, one to four channels, all components alike) is checked
    // here, and the size limit is the SL decoder profile (2048 px per decoded side) applied inside neo-j2k.
    U32 width = 0;
    U32 height = 0;
    U32 f = 0;
    bool image_ok = false;
    if (decoded)
    {
        width = image.comps[0].w;
        height = image.comps[0].h;
        f = image.comps[0].factor;
        image_ok = (channels >= 1 && channels <= 4
                    && width > 0 && height > 0
                    && width <= 65535u && height <= 65535u
                    && (S32)image.numcomps >= first_channel + channels);
        for (S32 comp = first_channel; image_ok && comp < first_channel + channels; comp++)
        {
            const nj2k_image_comp_t& c = image.comps[comp];
            image_ok = (c.w == width && c.h == height && c.factor == f);
        }
    }

    if (!decoded)
    {
        // reset the channel count if necessary
        if (raw_image.getComponents() != channels && channels >= 1)
        {
            raw_image.resize(raw_image.getWidth(), raw_image.getHeight(), S8(channels));
        }

        LL_WARNS("Texture") << "J2C decode failed (" << max_bytes << " bytes): "
                            << (decoder.last_error.empty() ? "no neo-j2k message" : decoder.last_error.c_str()) << LL_ENDL;
        LL_DEBUGS("Texture") << "ERROR -> decodeImpl: failed to decode image!" << LL_ENDL;
        // [SL:KB] - Patch: Viewer-OpenJPEG2 | Checked: Catznip-5.3
        base.decodeFailed();
        // [SL:KB]
        return true; // done
    }

    if (!image_ok)
    {
        LL_WARNS("Texture") << "decodeImpl: unsupported J2C geometry " << width << "x" << height << " channels " << channels << LL_ENDL;
        base.setLastError("Unsupported image geometry");
        base.decodeFailed();
        return true; // done
    }

    raw_image.resize(U16(width), U16(height), S8(channels));

    U8 *rawp = raw_image.getData();

    // <FS:Ansariel> Port fix for MAINT-4327/MAINT-6584 to OpenJPEG decoder
    if (!rawp)
    {
        base.setLastError("Memory error");
        base.decodeFailed();
        return true; // done
    }
    // <FS:Ansariel>

    // first_channel is what channel to start copying from, dest what channel to copy to. first_channel comes from
    // the argument, dest always starts writing at channel zero. The rows go in bottom-up (a negative row stride
    // from the last row of the buffer) and the samples interleaved, their low 8 bits, as the old copy loop wrote.
    nj2k_plane_t planes[4] = {};
    const S64 row_bytes = (S64)width * channels;
    for (S32 comp = first_channel, dest = 0; comp < first_channel + channels; comp++, dest++)
    {
        planes[dest].data = rawp + (S64)(height - 1) * row_bytes + dest;
        planes[dest].row_stride = -row_bytes;
        planes[dest].sample_stride = (NJ2K_UINT32)channels;
        planes[dest].component = (NJ2K_UINT32)comp;
    }
    nj2k_output_t output = {};
    output.struct_size = sizeof(output);
    output.sample_type = NJ2K_SAMPLE_U8;
    output.numplanes = (NJ2K_UINT32)channels;
    output.planes = planes;

    bool ok;
    {
        PoolLoan loan(decoder.codec); // lends the pool when this is a pool thread and the pool is idle
        ok = nj2k_decode_memory(decoder.codec, data, (size_t)max_bytes, &output);
    }
    if (!ok)
    {
        LL_WARNS("Texture") << "J2C decode failed (" << max_bytes << " bytes): "
                            << (decoder.last_error.empty() ? "no neo-j2k message" : decoder.last_error.c_str()) << LL_ENDL;
        LL_DEBUGS("Texture") << "ERROR -> decodeImpl: failed! (neo-j2k)" << LL_ENDL;
        // [SL:KB] - Patch: Viewer-OpenJPEG2 | Checked: Catznip-5.3
        base.decodeFailed();
        // [SL:KB]
        return true; // done
    }

    base.setDiscardLevel(f);

    return true; // done
}


bool LLImageJ2CNeo::encodeImpl(LLImageJ2C &base, const LLImageRaw &raw_image, const char* comment_text, F32 encode_time, bool reversible)
{
    if (raw_image.isBufferInvalid())
    {
        base.setLastError("Invalid input, no buffer");
        return false;
    }

    LLImageDataSharedLock lockIn(&raw_image);
    LLImageDataLock lockOut(&base);
    LLImageFPScope fpScope; // the rate allocation and the 9/7 quantisation are float: nearest even, whichever thread

    const S32 numcomps = raw_image.getComponents();
    const S32 width    = raw_image.getWidth();
    const S32 height   = raw_image.getHeight();

    nj2k_cparameters_t parameters;
    nj2k_set_default_encoder_parameters(&parameters);
    parameters.cod_format = NJ2K_CODEC_J2K;
    parameters.prog_order = NJ2K_RLCP; // should be the default, but, just in case
    parameters.cp_disto_alloc = 1;    // enable rate allocation by distortion
    parameters.max_cs_size = 0;       // do not cap max size because we're using tcp_rates and also irrelevant with lossless.

    if (reversible)
    {
        parameters.irreversible = 0; // should be the default, but, just in case
        parameters.tcp_numlayers = 1;
        // 0.0 is lossless (1.0 as the documentation says is not)
        parameters.tcp_rates[0] = 0.0f;
    }
    else
    {
        parameters.irreversible = 1;
    }

    std::string comment(comment_text ? comment_text : "no comment");
    parameters.cp_comment = &comment[0];

    // the planes of the image, from the interleaved bottom-up raw image
    std::vector<nj2k_image_cmptparm_t> cmptparm(numcomps);
    for (S32 c = 0; c < numcomps; c++)
    {
        cmptparm[c] = {};
        cmptparm[c].prec = 8;
        cmptparm[c].sgnd = 0;
        cmptparm[c].dx = parameters.subsampling_dx;
        cmptparm[c].dy = parameters.subsampling_dy;
        cmptparm[c].w = width;
        cmptparm[c].h = height;
    }
    std::unique_ptr<nj2k_image_t, ImageDeleter> image(nj2k_image_create(numcomps, cmptparm.data(), NJ2K_CLRSPC_SRGB));
    if (!image)
    {
        LL_WARNS() << "neo-j2k encoding was unsuccessful, returning false" << LL_ENDL;
        return false;
    }
    image->x1 = width;
    image->y1 = height;

    const U8* src_datap = raw_image.getData();
    S32 i = 0;
    for (S32 y = height - 1; y >= 0; y--)
    {
        for (S32 x = 0; x < width; x++)
        {
            const U8* pixel = src_datap + (y * width + x) * numcomps;
            for (S32 c = 0; c < numcomps; c++)
            {
                image->comps[c].data[i] = *pixel;
                pixel++;
            }
            i++;
        }
    }

    parameters.tcp_mct = (image->numcomps >= 3) ? 1 : 0; // no color transform for RGBA images

    // if not lossless compression, computes tcp_numlayers and max_cs_size depending on the image dimensions
    if (parameters.irreversible)
    {
        // computes a number of layers
        U32 surface = raw_image.getWidth() * raw_image.getHeight();

        // gets the necessary number of layers
        U32 nb_layers = estimate_num_layers(surface);

        // fills parameters.tcp_rates and updates parameters.tcp_numlayers
        set_tcp_rates(&parameters, nb_layers, LAST_TCP_RATE);
    }

    U32 width_tiles = (raw_image.getWidth() >> 6);
    U32 height_tiles = (raw_image.getHeight() >> 6);

    // Allow images with a width or height that are MIN_IMAGE_SIZE <= x < 64
    if (width_tiles == 0 && (raw_image.getWidth() >= MIN_IMAGE_SIZE))
    {
        width_tiles = 1;
    }
    if (height_tiles == 0 && (raw_image.getHeight() >= MIN_IMAGE_SIZE))
    {
        height_tiles = 1;
    }

    if (width_tiles == 1 || height_tiles == 1)
    {
        // Images with either dimension less than 32 need less number of resolutions otherwise they error
        int min_dim = raw_image.getWidth() < raw_image.getHeight() ? raw_image.getWidth() : raw_image.getHeight();
        int max_res = 1 + (int)floor(log2(min_dim));
        parameters.numresolution = max_res;
    }

    std::unique_ptr<nj2k_codec_t, CodecDeleter> encoder(nj2k_create_compress(NJ2K_CODEC_J2K));
    if (!encoder || !nj2k_setup_encoder(encoder.get(), &parameters, image.get()))
    {
        LL_WARNS() << "neo-j2k encoding was unsuccessful, returning false" << LL_ENDL;
        return false;
    }

    // the stream's buffer, sized as the old guess; the codestream itself grows in the sink
    const size_t TILE_SIZE = 64 * 64 * 3;
    const size_t data_size_guess = std::max<size_t>((size_t)width_tiles * height_tiles * TILE_SIZE, 4096);
    EncodeSink sink;
    std::unique_ptr<nj2k_stream_t, StreamDeleter> stream(nj2k_stream_create(data_size_guess, NJ2K_FALSE));
    if (!stream)
    {
        LL_WARNS() << "neo-j2k encoding was unsuccessful, returning false" << LL_ENDL;
        return false;
    }
    nj2k_stream_set_user_data(stream.get(), &sink, nullptr);
    nj2k_stream_set_write_function(stream.get(), sinkWrite);

    const bool encoded = nj2k_start_compress(encoder.get(), image.get(), stream.get())
                         && nj2k_encode(encoder.get(), stream.get())
                         && nj2k_end_compress(encoder.get(), stream.get());
    if (!encoded)
    {
        LL_WARNS() << "neo-j2k encoding was unsuccessful, returning false" << LL_ENDL;
        return false;
    }

    // "append" (set) the data we "streamed" for writing to the formatted image
    // with side-effect of setting the actually encoded size  to same
    U8* out = base.allocateData((S32)sink.bytes.size());
    if (!out)
    {
        LL_WARNS() << "neo-j2k encoding was unsuccessful, returning false" << LL_ENDL;
        return false;
    }
    memcpy(out, sink.bytes.data(), sink.bytes.size());
    base.updateData(); // update width, height etc from header
    return true;
}

bool LLImageJ2CNeo::getMetadata(LLImageJ2C &base)
{
    LLImageDataLock lock(&base);

    LLImageFPScope fpScope;
    ThreadDecoder& decoder = threadDecoder();
    ReduceReset reduceReset(decoder);

    nj2k_image_t image = {};
    if (!decoder.readHeader(base.getData(), base.getDataSize(), image))
    {
        // debug level: a partial download routinely fails its first header pass
        LL_DEBUGS("Texture") << "J2C header refused or unreadable (" << base.getDataSize() << " bytes): "
                             << (decoder.last_error.empty() ? "no neo-j2k message" : decoder.last_error.c_str()) << LL_ENDL;
        return false;
    }
    nj2k_codestream_layout_t layout = {};
    layout.struct_size = sizeof(layout);
    if (!nj2k_get_codestream_layout(decoder.codec, &layout))
    {
        return false;
    }

    // The size is the image area (x1 - x0), not the tile grid (tw * tdx): they differ for a multi-tile codestream
    // whose last tile is partial or that has an offset (the old size would be too large).
    const S32 width = S32(image.x1 - image.x0);
    const S32 height = S32(image.y1 - image.y0);
    const S32 components = (S32)image.numcomps;

    // LLImageBase keeps the component count in a signed byte, where 260 components read as 4: refuse
    // a count the viewer does not take (1 to MAX_IMAGE_COMPONENTS, llj2cdecodeguard.h) before anything is stored,
    // as a header that does not read is, instead of letting it wrap.
    if (!LLJ2CDecodeGuard::componentsSupported(components, MAX_IMAGE_COMPONENTS))
    {
        LL_WARNS("Texture") << "J2C header declares " << components << " components, refusing it" << LL_ENDL;
        base.setLastError("Unsupported number of image components");
        return false;
    }

    U32 tilesW = layout.tw;
    U32 tilesH = layout.th;
    S32 discard_level = 0;
    while (tilesW > 1 && tilesH > 1 && discard_level < MAX_DISCARD_LEVEL)
    {
        discard_level++;
        tilesW >>= 1;
        tilesH >>= 1;
    }

    // The resolutions of the component with the fewest: decode stays below them (llj2creduce.h). The rule also
    // bounds the decode by the size, which LLImageBase keeps in 16 bits: for a codestream larger than that, the
    // resolutions stay unknown (0) and its requests are passed on unchanged.
    S32 resolutions = 0;
    const U64 grid_width = (U64)layout.tw * layout.tdx;
    const U64 grid_height = (U64)layout.th * layout.tdy;
    if (grid_width <= 0xffff && grid_height <= 0xffff)
    {
        resolutions = (S32)layout.min_resolutions;
    }

    base.mDiscardLevel = discard_level;
    base.mNumResolutions = resolutions; // for decodeImpl's reduce factor
    base.setSize(width, height, components);
    return true;
}
