# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""A port, for the tests, of how the viewer turns a Windows command line holding --leap into plugin commands.

Ported from the source, not run in the viewer:
  LLCommandLineParser::parseCommandLineString (indra/newview/llcommandlineparser.cpp): backslash pre-processing, then
    boost::escaped_list_separator with escape "\\", separators "\\r\\n " and quote characters "\\"'";
  the LLSD branch of the option storing code (same file): more than one value -> an LLSD array of plain strings,
    one value -> setValueFromNotation (not simulated: it is only reported as the bare string);
  LLLeap::create(desc, string) (indra/llcommon/llleap.cpp): LLStringUtil::getTokens with delimiters " \\t\\r\\n",
    quotes "\\"'", escape "\\".
"""


def preprocess(s):
    """The doubling of backslashes in parseCommandLineString (so that boost's escape handling gives them back)."""
    if not s:
        return ""
    out = []
    last = len(s) - 1
    add_last = True
    pos = 0
    while pos < last:
        out.append(s[pos])
        if s[pos] == "\\":
            out.append("\\")
            if s[pos + 1] == "\\":
                pos += 1
                add_last = pos != last
        pos += 1
    if add_last:
        out.append(s[last])
        if s[last] == "\\":
            out.append("\\")
    return "".join(out)


def escaped_list_tokens(s, escape="\\", separators="\r\n ", quotes="\"'"):
    """boost::escaped_list_separator: quotes toggle (and are dropped) and protect separators; escape + char = char."""
    tokens, cur, in_quote, i = [], [], False, 0
    while i < len(s):
        c = s[i]
        if c == escape:
            i += 1
            if i >= len(s):
                raise ValueError("escape at the end")
            n = s[i]
            if n == "n":
                cur.append("\n")
            elif n == escape or n in quotes or n in separators:
                cur.append(n)
            else:
                raise ValueError("bad escape \\%s" % n)
        elif c in separators and not in_quote:
            tokens.append("".join(cur))
            cur = []
        elif c in quotes:
            in_quote = not in_quote
        else:
            cur.append(c)
        i += 1
    tokens.append("".join(cur))
    return tokens


def viewer_tokens(cmdline):
    return [t for t in escaped_list_tokens(preprocess(cmdline)) if t]


def leap_command(cmdline):
    """LeapCommand as the viewer stores it: a list of plain strings when --leap is given more than once, else the
    single value (which the viewer would then parse as LLSD notation)."""
    toks = viewer_tokens(cmdline)
    values = [toks[i + 1] for i, t in enumerate(toks[:-1]) if t == "--leap"]
    return values if len(values) > 1 else (values[0] if values else None)


def llleap_tokens(command):
    """LLStringUtil::getTokens(command, " \\t\\r\\n", "", "\\"'", "\\\\") as LLLeap::create uses it."""
    out, cur, quote, i, started = [], [], None, 0, False
    while i < len(command):
        c = command[i]
        if c == "\\" and i + 1 < len(command):
            i += 1
            cur.append(command[i])
            started = True
        elif quote:
            if c == quote:
                quote = None
            else:
                cur.append(c)
        elif c in "\"'":
            quote = c
            started = True
        elif c in " \t\r\n":
            if cur or started:
                out.append("".join(cur))
            cur, started = [], False
        else:
            cur.append(c)
            started = True
        i += 1
    if cur or started:
        out.append("".join(cur))
    return out
