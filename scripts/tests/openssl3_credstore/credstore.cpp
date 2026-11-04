// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
//
// Credential-store (bin_conf.dat) compatibility harness for the OpenSSL 3
// study (doc/openssl3-impact.md). It reproduces the protected-data read and
// write of indra/newview/llsechandler_basic.cpp (LLSecAPIBasicHandler::
// _readProtectedData / _writeProtectedData): a 16-byte salt, XORed with the
// 6-byte machine id (LLXORCipher), followed by the LLSD XML text encrypted
// with RC4 keyed by the salt, processed in 256-byte chunks.
//
// Built against OpenSSL 1.1.1 it runs the viewer's code as it is today.
// Built against OpenSSL 3 (VKS_OPENSSL3 defined) it runs, by default, the
// proposed code: a private OSSL_LIB_CTX with the legacy provider loaded in
// it, EVP_CIPHER_fetch("RC4"), every return value checked, and the store
// replaced only after the whole file has been written. --unchecked runs
// today's viewer code unchanged against OpenSSL 3 (default library context,
// no legacy provider) to show what an unmodified viewer would do.
//
// Usage:
//   credstore info
//   credstore write <id> <salt> <plaintext-in> <store-out> [options]
//   credstore read  <id> <store-in> <plaintext-out> [options]
//   credstore digest <id> <store-in> [options]
// <id>   12 hex digits, or "-" to read them from stdin (never echoed)
// <salt> 32 hex digits, or "-" for RAND_bytes as the viewer does
// options (OpenSSL 3 build only): --no-legacy  --unchecked
// digest prints only the SHA-256 of the plaintext, its length and the
// number of <key> elements; it never prints the plaintext, id or salt.
//
// Exit codes: 0 success, 1 crypto failure (nothing written), 2 usage or I/O.

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#ifdef VKS_OPENSSL3
#include <openssl/provider.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr int STORE_SALT_SIZE = 16;   // llsechandler_basic.cpp:58
constexpr int BUFFER_READ_SIZE = 256; // llsechandler_basic.cpp:59
constexpr int MAC_ADDRESS_BYTES = 6;  // llviewernetwork.h:264

// LLXORCipher (indra/llmessage/llxorcipher.cpp): a repeating pad whose
// position carries over between calls.
class XorCipher
{
public:
    XorCipher(const unsigned char* pad, size_t len) : mPad(pad, pad + len) {}
    void apply(unsigned char* buf, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            buf[i] ^= mPad[mHead++];
            if (mHead >= mPad.size())
                mHead = 0;
        }
    }

private:
    std::vector<unsigned char> mPad;
    size_t mHead = 0;
};

struct Options
{
    bool noLegacy = false;
    bool unchecked = false;
};

bool parseHex(const std::string& hex, unsigned char* out, size_t len)
{
    if (hex.size() != len * 2)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

bool readId(const std::string& arg, unsigned char* id)
{
    std::string hex = arg;
    if (arg == "-")
    {
        std::getline(std::cin, hex);
        while (!hex.empty() && (hex.back() == '\r' || hex.back() == '\n' || hex.back() == ' '))
            hex.pop_back();
    }
    return parseHex(hex, id, MAC_ADDRESS_BYTES);
}

bool readFile(const std::string& path, std::string& data)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    data = ss.str();
    return true;
}

bool fileExists(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

void printErrors()
{
    unsigned long e;
    while ((e = ERR_get_error()) != 0)
    {
        char buf[256];
        ERR_error_string_n(e, buf, sizeof(buf));
        std::cerr << "  openssl: " << buf << "\n";
    }
}

// The viewer's replace step (llsechandler_basic.cpp, after the write try).
void replaceStore(const std::string& tmp, const std::string& path)
{
    if ((fileExists(path) && std::remove(path.c_str()) != 0) || std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        std::cerr << "could not overwrite protected data store\n";
        std::remove(tmp.c_str());
    }
}

// ---------------------------------------------------------------------------
// Today's viewer code, statement for statement (return values ignored).
// ---------------------------------------------------------------------------
int viewerRead(const unsigned char* id, const std::string& storePath, std::string& decrypted_data)
{
    std::ifstream protected_data_stream(storePath, std::ios::binary);
    if (protected_data_stream.fail())
        return 2;
    unsigned char salt[STORE_SALT_SIZE];
    unsigned char buffer[BUFFER_READ_SIZE];
    unsigned char decrypted_buffer[BUFFER_READ_SIZE];
    int decrypted_length;
    XorCipher cipher(id, MAC_ADDRESS_BYTES);

    protected_data_stream.read(reinterpret_cast<char*>(salt), STORE_SALT_SIZE);
    if (protected_data_stream.gcount() < STORE_SALT_SIZE)
    {
        std::cerr << "Config file too short.\n";
        return 2;
    }
    cipher.apply(salt, STORE_SALT_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit(ctx, EVP_rc4(), salt, NULL);
    while (protected_data_stream.good())
    {
        protected_data_stream.read(reinterpret_cast<char*>(buffer), BUFFER_READ_SIZE);
        EVP_DecryptUpdate(ctx, decrypted_buffer, &decrypted_length, buffer,
                          static_cast<int>(protected_data_stream.gcount()));
        decrypted_data.append(reinterpret_cast<const char*>(decrypted_buffer),
                              static_cast<int>(protected_data_stream.gcount()));
    }
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

int viewerWrite(const unsigned char* id, const unsigned char* givenSalt, const std::string& plaintext,
                const std::string& storePath)
{
    std::istringstream formatted_data_istream(plaintext);
    unsigned char salt[STORE_SALT_SIZE];
    unsigned char buffer[BUFFER_READ_SIZE];
    unsigned char encrypted_buffer[BUFFER_READ_SIZE];
    if (givenSalt)
        std::memcpy(salt, givenSalt, STORE_SALT_SIZE);
    else
        RAND_bytes(salt, STORE_SALT_SIZE);

    std::string tmp_filename = storePath + ".tmp";
    std::ofstream protected_data_stream(tmp_filename, std::ios::binary);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit(ctx, EVP_rc4(), salt, NULL);
    XorCipher cipher(id, MAC_ADDRESS_BYTES);
    cipher.apply(salt, STORE_SALT_SIZE);
    protected_data_stream.write(reinterpret_cast<const char*>(salt), STORE_SALT_SIZE);
    while (formatted_data_istream.good())
    {
        formatted_data_istream.read(reinterpret_cast<char*>(buffer), BUFFER_READ_SIZE);
        if (formatted_data_istream.gcount() == 0)
            break;
        int encrypted_length;
        EVP_EncryptUpdate(ctx, encrypted_buffer, &encrypted_length, buffer,
                          static_cast<int>(formatted_data_istream.gcount()));
        protected_data_stream.write(reinterpret_cast<const char*>(encrypted_buffer), encrypted_length);
    }
    EVP_CIPHER_CTX_free(ctx);
    protected_data_stream.close();
    replaceStore(tmp_filename, storePath);
    return 0;
}

#ifdef VKS_OPENSSL3
// ---------------------------------------------------------------------------
// Proposed OpenSSL 3 code: private library context with the legacy
// provider, explicit fetch, every return checked, nothing written on error.
// ---------------------------------------------------------------------------
class Rc4
{
public:
    explicit Rc4(bool loadLegacy)
    {
        mLibCtx = OSSL_LIB_CTX_new();
        if (!mLibCtx)
            return;
        if (loadLegacy)
        {
            mLegacy = OSSL_PROVIDER_load(mLibCtx, "legacy");
            if (!mLegacy)
                return;
        }
        mCipher = EVP_CIPHER_fetch(mLibCtx, "RC4", nullptr);
    }
    ~Rc4()
    {
        EVP_CIPHER_free(mCipher);
        if (mLegacy)
            OSSL_PROVIDER_unload(mLegacy);
        OSSL_LIB_CTX_free(mLibCtx);
    }
    // Runs the whole buffer through RC4 in BUFFER_READ_SIZE chunks.
    bool run(const unsigned char* key, bool encrypt, const std::string& in, std::string& out) const
    {
        if (!mCipher || EVP_CIPHER_get_key_length(mCipher) != STORE_SALT_SIZE)
            return false;
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
            return false;
        bool ok = EVP_CipherInit_ex2(ctx, mCipher, key, nullptr, encrypt ? 1 : 0, nullptr) == 1;
        unsigned char outbuf[BUFFER_READ_SIZE];
        for (size_t pos = 0; ok && pos < in.size(); pos += BUFFER_READ_SIZE)
        {
            int inl = static_cast<int>(std::min<size_t>(BUFFER_READ_SIZE, in.size() - pos));
            int outl = 0;
            ok = EVP_CipherUpdate(ctx, outbuf, &outl, reinterpret_cast<const unsigned char*>(in.data()) + pos, inl) == 1
                 && outl == inl;
            if (ok)
                out.append(reinterpret_cast<const char*>(outbuf), outl);
        }
        int finl = 0;
        ok = ok && EVP_CipherFinal_ex(ctx, outbuf, &finl) == 1 && finl == 0;
        EVP_CIPHER_CTX_free(ctx);
        return ok;
    }

private:
    OSSL_LIB_CTX* mLibCtx = nullptr;
    OSSL_PROVIDER* mLegacy = nullptr;
    EVP_CIPHER* mCipher = nullptr;
};

int proposedRead(const unsigned char* id, const std::string& storePath, const Options& opt, std::string& plaintext)
{
    std::string file;
    if (!readFile(storePath, file))
        return 2;
    if (file.size() < STORE_SALT_SIZE)
    {
        std::cerr << "Config file too short.\n";
        return 2;
    }
    unsigned char salt[STORE_SALT_SIZE];
    std::memcpy(salt, file.data(), STORE_SALT_SIZE);
    XorCipher(id, MAC_ADDRESS_BYTES).apply(salt, STORE_SALT_SIZE);
    Rc4 rc4(!opt.noLegacy);
    std::string out;
    if (!rc4.run(salt, false, file.substr(STORE_SALT_SIZE), out))
    {
        std::cerr << "RC4 unavailable or failed: store not decrypted\n";
        printErrors();
        return 1;
    }
    plaintext.swap(out);
    return 0;
}

int proposedWrite(const unsigned char* id, const unsigned char* givenSalt, const std::string& plaintext,
                  const std::string& storePath, const Options& opt)
{
    unsigned char salt[STORE_SALT_SIZE];
    if (givenSalt)
        std::memcpy(salt, givenSalt, STORE_SALT_SIZE);
    else if (RAND_bytes(salt, STORE_SALT_SIZE) != 1)
    {
        std::cerr << "RAND_bytes failed: nothing written\n";
        return 1;
    }
    Rc4 rc4(!opt.noLegacy);
    std::string body;
    if (!rc4.run(salt, true, plaintext, body))
    {
        std::cerr << "RC4 unavailable or failed: nothing written, store untouched\n";
        printErrors();
        return 1;
    }
    XorCipher(id, MAC_ADDRESS_BYTES).apply(salt, STORE_SALT_SIZE);

    // Write the complete file, then replace the store only if that worked.
    std::string tmp = storePath + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(salt), STORE_SALT_SIZE);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.close();
        if (!out)
        {
            std::remove(tmp.c_str());
            std::cerr << "write failed: store untouched\n";
            return 2;
        }
    }
    replaceStore(tmp, storePath);
    return 0;
}
#endif

void printDigest(const std::string& plaintext)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdlen = 0;
    if (EVP_Digest(plaintext.data(), plaintext.size(), md, &mdlen, EVP_sha256(), nullptr) != 1)
    {
        std::cout << "sha256 unavailable\n";
        return;
    }
    size_t keys = 0;
    for (size_t pos = plaintext.find("<key>"); pos != std::string::npos; pos = plaintext.find("<key>", pos + 5))
        ++keys;
    bool llsd = plaintext.find("<llsd>") != std::string::npos && plaintext.find("</llsd>") != std::string::npos;
    std::cout << "sha256=";
    for (unsigned i = 0; i < mdlen; ++i)
    {
        char h[3];
        std::snprintf(h, sizeof(h), "%02x", md[i]);
        std::cout << h;
    }
    std::cout << " length=" << plaintext.size() << " keys=" << keys << " llsd=" << (llsd ? "yes" : "no") << "\n";
}

int usage()
{
    std::cerr << "usage: credstore info | write <id> <salt> <in> <store> | read <id> <store> <out> | "
                 "digest <id> <store>  [--no-legacy] [--unchecked]\n";
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--no-legacy")
            opt.noLegacy = true;
        else if (a == "--unchecked")
            opt.unchecked = true;
        else
            args.push_back(a);
    }
#ifndef VKS_OPENSSL3
    if (opt.noLegacy || opt.unchecked)
    {
        std::cerr << "--no-legacy and --unchecked apply to the OpenSSL 3 build only\n";
        return 2;
    }
    opt.unchecked = true; // the 1.1.1 build is today's viewer code
#endif
    if (args.empty())
        return usage();

    if (args[0] == "info")
    {
#ifdef VKS_OPENSSL3
        std::cout << OpenSSL_version(OPENSSL_VERSION) << " | " << OpenSSL_version(OPENSSL_CFLAGS) << "\n";
#else
        std::cout << OpenSSL_version(OPENSSL_VERSION) << "\n";
#endif
        return 0;
    }

    unsigned char id[MAC_ADDRESS_BYTES];
    if (args.size() < 3 || !readId(args[1], id))
        return usage();

    if (args[0] == "write" && args.size() == 5)
    {
        unsigned char salt[STORE_SALT_SIZE];
        const unsigned char* givenSalt = nullptr;
        if (args[2] != "-")
        {
            if (!parseHex(args[2], salt, STORE_SALT_SIZE))
                return usage();
            givenSalt = salt;
        }
        std::string plaintext;
        if (!readFile(args[3], plaintext))
            return 2;
#ifdef VKS_OPENSSL3
        if (!opt.unchecked)
            return proposedWrite(id, givenSalt, plaintext, args[4], opt);
#endif
        return viewerWrite(id, givenSalt, plaintext, args[4]);
    }

    if ((args[0] == "read" && args.size() == 4) || (args[0] == "digest" && args.size() == 3))
    {
        std::string plaintext;
        int rc;
#ifdef VKS_OPENSSL3
        if (!opt.unchecked)
            rc = proposedRead(id, args[2], opt, plaintext);
        else
#endif
            rc = viewerRead(id, args[2], plaintext);
        if (rc != 0)
            return rc;
        if (args[0] == "digest")
        {
            printDigest(plaintext);
            return 0;
        }
        std::ofstream out(args[3], std::ios::binary | std::ios::trunc);
        out.write(plaintext.data(), static_cast<std::streamsize>(plaintext.size()));
        return out ? 0 : 2;
    }
    return usage();
}
