// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Standalone test of the generated-shader manifest parse (llgeneratedmanifest.h), on small literals and on the committed
// app_settings/shaders/generated/programs.json. Usage: llgeneratedmanifest_test [path to programs.json].
#include <boost/json/src.hpp>   // header-only boost::json: this one translation unit carries its implementation

#include "llgeneratedmanifest.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond); std::exit(1); } } while (0)

static const char* kSmall = R"({"generator":1,"programs":[{"id":"p","stages":{
 "vertex":{"file":"p.vert.glsl","interface":{"inputs":[{"name":"position","location":0},{"name":"texcoord0","location":2}],
   "uniforms":[{"name":"mvp","offset":0,"size":64},{"name":"alpha","offset":64,"size":4}],"textures":[]}},
 "fragment":{"file":"p.frag.glsl","interface":{"inputs":[],"uniforms":[{"name":"alpha","offset":64,"size":4}],
   "textures":[{"name":"diffuseMap","binding":0},{"name":"bumpMap","binding":1}]}}}}]})";

int main(int argc, char** argv)
{
    using namespace LLGeneratedShader;
    std::map<std::string, StageInterface> out;
    std::string error;

    CHECK(parseManifest(kSmall, out, error));
    CHECK(out.size() == 2);
    const StageInterface& v = out.at("p.vert.glsl");
    CHECK(v.inputs.size() == 2 && v.inputs[1].name == "texcoord0" && v.inputs[1].location == 2);
    CHECK(v.uniforms.size() == 2 && v.uniforms[0].name == "mvp" && v.uniforms[0].offset == 0 && v.uniforms[0].size == 64);
    CHECK(v.uniforms[1].offset == 64 && v.uniforms[1].size == 4);
    const StageInterface& f = out.at("p.frag.glsl");
    CHECK(f.textures.size() == 2 && f.textures[1].name == "bumpMap" && f.textures[1].binding == 1);

    // not JSON, not an object, no programs, a stage without its interface or a key: false, nothing half loaded
    CHECK(!parseManifest("", out, error) && out.empty() && !error.empty());
    CHECK(!parseManifest("{not json", out, error) && out.empty());
    CHECK(!parseManifest("[]", out, error) && out.empty());
    CHECK(!parseManifest("{\"programs\": 3}", out, error) && out.empty());
    CHECK(!parseManifest("{\"programs\":[{\"stages\":{\"vertex\":{\"file\":\"a\"}}}]}", out, error) && out.empty());
    {
        std::string broken = kSmall;
        broken.replace(broken.find("\"binding\":1"), 11, "\"bindin\":1");
        CHECK(!parseManifest(broken, out, error) && out.empty());   // the first stage parsed, the second failed: none kept
    }
    {
        std::string negative = kSmall;
        negative.replace(negative.find("\"offset\":64"), 11, "\"offset\":-4");
        CHECK(!parseManifest(negative, out, error) && out.empty());   // a negative offset would wrap to a huge unsigned
    }
    CHECK(parseManifest("{\"programs\":[]}", out, error) && out.empty());

    // the committed manifest
    const std::string path = argc > 1 ? argv[1] : "indra/newview/app_settings/shaders/generated/programs.json";
    std::ifstream in(path, std::ios::binary);
    CHECK(in.good());
    std::stringstream text;
    text << in.rdbuf();
    CHECK(parseManifest(text.str(), out, error));
    CHECK(out.size() >= 20);   // a does-it-parse floor: the manifest holds well over 20 stage files; a truncated parse would fall far below
    size_t stages_with_block = 0;
    for (const auto& entry : out)
    {
        const StageInterface& s = entry.second;
        const std::string& name = entry.first;
        CHECK(isGeneratedFile("generated/" + name));
        unsigned end = 0;
        std::set<std::string> seen;
        for (const auto& u : s.uniforms)
        {
            CHECK(!u.name.empty() && seen.insert(u.name).second);
            CHECK(u.size > 0 && u.offset % 4 == 0 && u.offset >= end);   // members are listed in block order and never overlap
            end = u.offset + u.size;
        }
        stages_with_block += !s.uniforms.empty();
        std::set<int> bindings;
        for (const auto& t : s.textures)
        {
            CHECK(t.binding >= 0 && bindings.insert(t.binding).second);
        }
        std::set<int> locations;
        for (const auto& i : s.inputs)
        {
            CHECK(i.location >= 0 && locations.insert(i.location).second);
        }
        if (name.size() > 10 && name.compare(name.size() - 10, 10, ".vert.glsl") == 0)
        {
            CHECK(!s.inputs.empty());   // every vertex stage reads at least the position
        }
    }
    CHECK(stages_with_block > 20);   // the same floor, for stages that carry a uniform block
    std::puts("llgeneratedmanifest_test: ok");
    return 0;
}
