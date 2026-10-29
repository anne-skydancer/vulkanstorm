/**
 * @file llinventory_offer_oracle.h
 * @brief Recorder R4b (inventory rung): the corpora of the offer and give
 * oracle. Each corpus runs the frozen copies (with the fakes) and the real
 * message system over a fixed case list or a seeded sweep and returns its
 * canonical lines, "<case>\t<key>\t<value>". Keys starting with "note." are
 * for review only: they are neither compared nor digested.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORY_OFFER_ORACLE_H
#define LL_LLINVENTORY_OFFER_ORACLE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace r4b_oracle
{
    // A corpus's output, streamed: every line goes to the sink; a corpus that
    // is committed in full keeps all of them, a sweep keeps a digest.
    struct Sink
    {
        virtual ~Sink() {}
        virtual void line(const std::string& l) = 0;
    };

    // SHA-256 of the canonical stream: every line that is not a note, each
    // followed by "\n".
    class Sha256
    {
    public:
        Sha256();
        void update(const void* data, size_t len);
        std::string hexdigest();
    private:
        void block(const uint8_t* p);
        uint32_t h[8];
        uint8_t buf[64];
        size_t buflen = 0;
        uint64_t total = 0;
    };

    bool is_note(const std::string& line);

    struct Corpus
    {
        std::string area;   // "offer" or "give"
        std::string name;   // file stem
        std::string what;   // one line for the header
        bool digest_only;   // a sweep: digest + sample committed, not the full text
        uint64_t seed;      // 0 for fixed case lists
        // runs the corpus
        std::function<void(Sink&)> run;
        // a digest corpus commits the cases with in_sample(case, modulus)
        uint32_t sample_modulus = 0;
    };

    const std::vector<Corpus>& corpora();

    // The digest corpora's sampling rule: a case is in the sample when the
    // FNV-1a hash of its id modulo `modulus` is 0.
    bool in_sample(const std::string& case_id, uint32_t modulus);

    // Initialise the message system with the real template (once).
    void init_messaging(const std::string& template_file);

    // Live (real pack_instant_message) against frozen (copy of
    // pack_instant_message_block), seeded; returns the number of differing
    // cases and fills `first` with the first difference.
    int pack_live_vs_frozen(uint64_t seed, int count, std::string& first);
}

#endif
