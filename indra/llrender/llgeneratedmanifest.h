// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The generated-shader manifest (app_settings/shaders/generated/programs.json) as the loader reads it: every stage's file
// name mapped to its interface (vertex inputs, block members with byte offsets, textures with units). Free of GL and of the
// viewer, so the parse is tested on its own (tests/llgeneratedmanifest_test.cpp) against the committed manifest.
#ifndef LL_LLGENERATEDMANIFEST_H
#define LL_LLGENERATEDMANIFEST_H

#include "llgeneratedshader.h"

#include <boost/json.hpp>

#include <exception>
#include <map>
#include <string>
#include <string_view>

namespace LLGeneratedShader
{
    // Parses the manifest text into `out` (keyed by the stage file's name under generated/). Returns false and leaves `out`
    // empty with `error` set when the text is not valid JSON or lacks a key the loader reads; a stale or hand-edited
    // manifest then fails every generated program cleanly instead of loading half of one.
    inline bool parseManifest(std::string_view text, std::map<std::string, StageInterface>& out, std::string& error)
    {
        out.clear();
        boost::system::error_code ec;
        boost::json::value root = boost::json::parse(text, ec);
        if (ec || !root.is_object() || !root.as_object().if_contains("programs") || !root.at("programs").is_array())
        {
            error = "not valid JSON, or no programs array";
            return false;
        }
        try
        {
            for (const boost::json::value& program : root.at("programs").as_array())
            {
                for (const auto& stage : program.at("stages").as_object())
                {
                    const boost::json::object& info = stage.value().as_object();
                    const boost::json::object& iface = info.at("interface").as_object();
                    StageInterface si;
                    for (const boost::json::value& v : iface.at("inputs").as_array())
                    {
                        const boost::json::object& o = v.as_object();
                        si.inputs.push_back({ std::string(o.at("name").as_string()), (int) o.at("location").to_number<int64_t>() });
                    }
                    for (const boost::json::value& v : iface.at("uniforms").as_array())
                    {
                        const boost::json::object& o = v.as_object();
                        si.uniforms.push_back({ std::string(o.at("name").as_string()), (unsigned) o.at("offset").to_number<int64_t>(),
                                                (unsigned) o.at("size").to_number<int64_t>() });
                    }
                    for (const boost::json::value& v : iface.at("textures").as_array())
                    {
                        const boost::json::object& o = v.as_object();
                        si.textures.push_back({ std::string(o.at("name").as_string()), (int) o.at("binding").to_number<int64_t>() });
                    }
                    out[std::string(info.at("file").as_string())] = std::move(si);
                }
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
            out.clear();
            return false;
        }
        return true;
    }
}

#endif // LL_LLGENERATEDMANIFEST_H
