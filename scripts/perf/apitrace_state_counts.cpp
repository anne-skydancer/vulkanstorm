// Offline analyzer built against apitrace 14.0's parser. No GL execution.
// Same requested state is a diagnostic candidate, not a removable-call proof.
#include "trace_parser.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>

using U = uint64_t;
constexpr U unknown = ~U(0);
using Counts = std::map<std::string, U>;
struct Definition { std::array<U, 8> value; U shaderEpoch; };
struct Context {
    U active = unknown, vao = unknown, buffer = unknown, shader = unknown, epoch = 0;
    std::map<std::pair<U, U>, U> textures;
    std::map<std::pair<U, U>, Definition> attributes;
    Counts total, window;
    void clear() { active = vao = buffer = shader = unknown; textures.clear(); attributes.clear(); }
    void count(const std::string &key, bool selected) { ++total[key]; if(selected) ++window[key]; }
};
bool begins(std::string_view value, std::string_view prefix) { return value.substr(0, prefix.size()) == prefix; }
void printCounts(const Counts &counts) {
    std::cout << '{'; bool comma = false;
    for (const auto &kv : counts) { if(comma) std::cout << ','; comma = true; std::cout << '"' << kv.first << "\":" << kv.second; }
    std::cout << '}';
}
int main(int argc, char **argv) {
    if (argc != 4) { std::cerr << "usage: apitrace_state_counts trace start-call end-call-exclusive\n"; return 2; }
    U start = std::stoull(argv[2]), end = std::stoull(argv[3]);
    if(end <= start) return 2;
    trace::Parser parser;
    if(!parser.open(argv[1])) return 1;
    std::map<U, Context> contexts;
    std::map<unsigned, U> current;
    Counts invalidations;
    U calls = 0, lastCall = 0, completedFrames = 0;
    auto invalidate = [&](std::string_view why) { ++invalidations[std::string(why)]; for(auto &kv : contexts) kv.second.clear(); };
    while (auto raw = parser.parse_call()) {
        std::unique_ptr<trace::Call> call(raw);
        ++calls; lastCall = call->no;
        if(call->flags & trace::CALL_FLAG_END_FRAME) ++completedFrames;
        std::string_view name(call->sig->name);
        if(call->flags & trace::CALL_FLAG_FAKE) {
            if(name != "glViewport" && name != "glScissor" && name != "glBindAttribLocation") invalidate("synthetic_state");
            else ++invalidations["synthetic_nonbinding_skipped"];
            continue;
        }
        bool selected = call->no >= start && call->no < end;
        auto u = [&](unsigned i) -> U { return call->arg(i).toUInt(); };
        auto ptr = [&](unsigned i) -> U { return call->arg(i).toUIntPtr(); };
        if(name == "wglMakeCurrent" || name == "wglMakeContextCurrentARB") {
            if(call->ret && call->ret->toBool()) {
                U ctx = ptr(name == "wglMakeCurrent" ? 1 : 2);
                current[call->thread_id] = ctx;
                if(ctx) contexts.try_emplace(ctx);
            }
            continue;
        }
        if(begins(name,"wgl") && (name.find("CreateContext") != name.npos || name.find("DeleteContext") != name.npos || name == "wglShareLists" || name == "wglCopyContext")) {
            ++invalidations[std::string(name)];
            if(name.find("CreateContext") != name.npos && call->ret && call->ret->toUIntPtr()) contexts[call->ret->toUIntPtr()].clear();
            else if(name.find("DeleteContext") != name.npos && call->ret && call->ret->toBool()) {
                U dead = ptr(0); contexts[dead].clear();
                for(auto &kv : current) if(kv.second == dead) kv.second = 0;
            } else if(name == "wglShareLists") {
                for(auto &kv : contexts) { kv.second.textures.clear(); kv.second.attributes.clear(); }
            } else if(name == "wglCopyContext") invalidate("wglCopyContext unmodeled copy");
            continue;
        }
        if(!(begins(name,"glActive") || begins(name,"glBind") || begins(name,"glVertexAttrib") || begins(name,"glVertexArray") ||
             begins(name,"glMultiTex") || begins(name,"glPushAttrib") || begins(name,"glPopAttrib") || begins(name,"glNewList") || begins(name,"glEndList") ||
             begins(name,"glCallList") || begins(name,"glGen") || begins(name,"glDelete") || begins(name,"glCreate") || name == "glUseProgram" ||
             name == "wglSwapBuffers" || name == "SwapBuffers" || name == "wglSwapLayerBuffers")) continue;
        U ctx = current[call->thread_id];
        if(!ctx) { if(begins(name,"gl")) invalidate("GL_without_context"); continue; }
        Context &s = contexts[ctx];
        if(name == "wglSwapBuffers" || name == "SwapBuffers" || name == "wglSwapLayerBuffers") { s.count("swap_calls",selected); continue; }
        if(begins(name,"glGen") || begins(name,"glDelete") || begins(name,"glCreate")) {
            ++invalidations[std::string(name)];
            for(auto &kv : contexts) {
                auto &other = kv.second;
                if(name.find("Texture") != name.npos) other.textures.clear();
                else if(name.find("VertexArray") != name.npos) { other.attributes.clear(); if(begins(name,"glDelete")) other.vao = unknown; }
                else if(name.find("Buffer") != name.npos) { other.attributes.clear(); if(begins(name,"glDelete")) other.buffer = unknown; }
            }
            continue;
        }
        if(name == "glActiveTexture") {
            U unit = u(0); s.count("active_texture_calls",selected);
            if(unit == s.active) s.count("active_texture_same_requested_unit",selected);
            s.active = unit; continue;
        }
        if(name == "glBindVertexArray") { s.vao = u(0); continue; }
        if(name == "glBindBuffer") { if(u(0) == 0x8892) s.buffer = u(1); continue; }
        if(name == "glUseProgram") { if(s.shader != u(0)) { s.shader = u(0); ++s.epoch; } continue; }
        if(name == "glBindTexture") {
            s.count("texture_bind_calls",selected);
            auto key = std::make_pair(s.active,u(0)); auto prev = s.textures.find(key);
            if(s.active == unknown || prev == s.textures.end()) s.count("texture_bind_unknown_prior_state",selected);
            else if(prev->second == u(1)) s.count("texture_bind_same_requested_state",selected);
            else s.count("texture_bind_changed_state",selected);
            if(s.active != unknown) s.textures[key] = u(1);
            continue;
        }
        if(name == "glVertexAttribPointer" || name == "glVertexAttribIPointer" || name == "glVertexAttribLPointer") {
            bool normal = name == "glVertexAttribPointer";
            std::array<U,8> value = {normal ? 0u : name == "glVertexAttribIPointer" ? 1u : 2u, s.buffer,u(1),u(2),normal?u(3):0u,u(normal?4:3),ptr(normal?5:4),0};
            auto key = std::make_pair(s.vao,u(0)); auto prev = s.attributes.find(key);
            bool known = s.vao != unknown && s.buffer != unknown && s.buffer != 0;
            s.count("attribute_pointer_calls",selected);
            if(!known || prev == s.attributes.end()) s.count("attribute_pointer_unknown_prior_state",selected);
            else if(prev->second.value == value) {
                s.count("attribute_pointer_same_requested_state",selected);
                if(prev->second.shaderEpoch != s.epoch) s.count("attribute_pointer_repeat_across_shader_change",selected);
            } else s.count("attribute_pointer_changed_state",selected);
            if(known) s.attributes[key] = {value,s.epoch};
            continue;
        }
        if(name == "glBindFramebuffer" || name == "glBindRenderbuffer" || name == "glBindSampler" || name == "glBindAttribLocation" ||
           name == "glBindFragDataLocation" || name == "glBindFragDataLocationIndexed" || name == "glBindImageTexture" || name == "glBindImageTextures" ||
           name == "glBindBufferBase" || name == "glBindBufferRange") continue;
        invalidate(name);
    }
    std::cout << "{\"status\":\"diagnostic_only_check_parser_stderr_for_truncation\",\"parsed_calls\":" << calls
              << ",\"last_call\":" << lastCall << ",\"completed_frame_flags\":" << completedFrames
              << ",\"start_call\":" << start << ",\"end_call_exclusive\":" << end << ",\"invalidations\":";
    printCounts(invalidations); std::cout << ",\"contexts\":{"; bool comma = false;
    for(auto &kv : contexts) {
        if(comma) std::cout << ','; comma = true;
        std::cout << '"' << kv.first << "\":{\"total\":"; printCounts(kv.second.total);
        std::cout << ",\"window\":"; printCounts(kv.second.window); std::cout << '}';
    }
    std::cout << "}}\n";
}
