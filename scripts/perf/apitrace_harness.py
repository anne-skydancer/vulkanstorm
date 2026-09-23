"""External GL capture preparation and conservative offline state analysis.

No viewer edits, GL queries, replay, or automatic process termination. Exact
state repeats are diagnostic candidates, never proof that a call is removable.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


# Keep context and resource mutations; filtering to just the counted calls would
# silently produce false duplicates. Unknown state-changing calls invalidate.
DUMP_FILTER = r"^(gl|wgl|SwapBuffers)"
CALL = re.compile(r"^(\d+)\s+@([0-9a-fA-F]+)\s+(\w+)\(")


def parse_call(line):
    match = CALL.match(line)
    if not match:
        raise ValueError("Unsupported dump line; require thread IDs and named arguments: " + line[:120])
    depth, quoted, escaped = 1, False, False
    for index in range(match.end(), len(line)):
        char = line[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                tail = line[index + 1:].strip()
                if tail and not tail.startswith("= "):
                    raise ValueError("Unsupported call suffix: " + tail[:80])
                return (*match.groups(), line[match.end():index], tail[2:] if tail else None)
    raise ValueError("Incomplete dump call; use --multiline=no")


def digest(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    with open(path, "x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def fields(text):
    """Split named arguments without splitting arrays, quoted strings or blobs."""
    parts, start, depth, quoted, escaped = [], 0, 0, False, False
    for i, char in enumerate(text):
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    parts.append(text[start:].strip())
    result = {}
    for part in parts:
        if part:
            key, sep, value = part.partition(" = ")
            if not sep:
                raise ValueError("dump requires --arg-names=yes")
            result[key] = value
    return result


def number(value):
    if value in ("NULL", "GL_FALSE", "FALSE"):
        return 0
    if value in ("GL_TRUE", "TRUE"):
        return 1
    return int(value, 16 if value.lower().startswith("0x") else 10)


class State:
    def __init__(self):
        self.stats = Counter()
        self.examples = []
        self.frame = 0
        self.clear()

    def clear(self):
        self.active = self.vao = self.array = None
        self.textures, self.attributes = {}, {}
        self.shader = None
        self.shader_epoch = 0


def analyze(lines, start_frame=0, end_frame=None):
    states, threads = {}, {}
    reasons, renderer = Counter(), {}
    parsed = 0

    def invalidate(reason):
        reasons[reason] += 1
        for state in states.values():
            state.clear()

    for line in lines:
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        if line.endswith(" // fake"):
            # apitrace synthesizes initial state for replay; those are not
            # application-issued calls and must never count as viewer work.
            _, _, synthetic_name, _, _ = parse_call(line[:-8])
            if synthetic_name not in ("glViewport", "glScissor", "glBindAttribLocation"):
                invalidate("apitrace synthetic state call")
            else:
                reasons["apitrace synthetic nonbinding call skipped"] += 1
            continue
        header = CALL.match(line)
        if not header:
            raise ValueError("Unsupported dump line: " + line[:120])
        name = header[3]
        # Full traces include enormous shader/uniform/upload payloads. Preserve
        # their call count without parsing arguments that cannot affect this
        # deliberately limited binding-state model.
        if not name.startswith(("wgl", "SwapBuffers", "glActive", "glBind", "glVertexAttrib", "glVertexArray",
                                "glMultiTex", "glPushAttrib", "glPopAttrib", "glNewList", "glEndList", "glCallList",
                                "glDelete", "glGen", "glCreate", "glUseProgram", "glGetString",
                                "glEnableVertexAttrib", "glDisableVertexAttrib")):
            parsed += 1
            continue
        call_no, thread, name, raw, result = parse_call(line)
        args = fields(raw)
        parsed += 1
        if name in ("wglMakeCurrent", "wglMakeContextCurrentARB"):
            if result is None:
                raise ValueError("Missing WGL call result")
            if number(result):
                context = args.get("hglrc", args.get("hglrcContext"))
                if context is None:
                    raise ValueError("Unknown WGL context argument")
                context = str(number(context))
                threads[thread] = context if context != "0" else None
                if context != "0":
                    states.setdefault(context, State())
            continue
        if name.startswith("wgl") and any(x in name for x in ("DeleteContext", "ShareLists", "CopyContext", "CreateContext")):
            reasons[name] += 1
            if "CreateContext" in name and result is not None and number(result):
                key = str(number(result))
                states.setdefault(key, State()).clear()
            elif "DeleteContext" in name and result is not None and number(result):
                key = str(number(args["hglrc"]))
                if key in states:
                    states[key].clear()
                threads = {thread: ctx if ctx != key else None for thread, ctx in threads.items()}
            elif "ShareLists" in name:
                for other in states.values():
                    other.textures.clear()
                    other.attributes.clear()
            elif "CopyContext" in name:
                invalidate(name + " unmodeled copy")
            continue
        context = threads.get(thread)
        if not context:
            if name.startswith("gl"):
                invalidate("GL call without known current context")
            continue
        state = states[context]
        selected = state.frame >= start_frame and (end_frame is None or state.frame < end_frame)
        if name in ("SwapBuffers", "wglSwapBuffers", "wglSwapLayerBuffers"):
            if selected:
                state.stats["swap_calls"] += 1
            state.frame += 1
            continue
        if name == "glGetString" and args.get("name") in ("GL_RENDERER", "GL_VENDOR", "GL_VERSION"):
            renderer.setdefault(context, {})[args["name"]] = result
        if name.startswith("glDelete") or name.startswith("glGen") or name.startswith("glCreate"):
            # Clear relevant object knowledge in every context because share
            # groups may be unknown. Generating names does not reset the active
            # unit/VAO, and deleting a texture must not erase the current VAO.
            reasons[name] += 1
            for other in states.values():
                if "Texture" in name:
                    other.textures.clear()
                elif "VertexArray" in name:
                    other.attributes.clear()
                    if name.startswith("glDelete"):
                        other.vao = None
                elif "Buffer" in name:
                    other.attributes.clear()
                    if name.startswith("glDelete"):
                        other.array = None
            continue
        kind, key, value, table = None, None, None, None
        try:
            if name == "glActiveTexture":
                token = args["texture"]
                unit = int(token[10:]) if token.startswith("GL_TEXTURE") else number(token) - 0x84C0
                if unit < 0:
                    raise ValueError("invalid texture unit")
                if selected:
                    state.stats["active_texture_calls"] += 1
                    state.stats["active_texture_same_requested_unit"] += state.active == unit
                state.active = unit
                continue
            if name == "glBindVertexArray":
                state.vao = number(args["array"])
                continue
            if name == "glBindBuffer":
                if args["target"] == "GL_ARRAY_BUFFER":
                    state.array = number(args["buffer"])
                continue
            if name == "glUseProgram":
                program = number(args["program"])
                if program != state.shader:
                    state.shader_epoch += 1
                    state.shader = program
                continue
            if name == "glBindTexture":
                kind, table = "texture_bind", state.textures
                key = (state.active, args["target"])
                value = number(args["texture"])
                known = state.active is not None
            elif name in ("glVertexAttribPointer", "glVertexAttribIPointer", "glVertexAttribLPointer"):
                kind, table = "attribute_pointer", state.attributes
                key = (state.vao, number(args["index"]))
                # Client-memory pointers are outside this VBO analyzer's scope.
                known = state.vao is not None and state.array not in (None, 0)
                value = (name, state.array, args["size"], args["type"],
                         args.get("normalized"), args["stride"], number(args["pointer"]))
            elif name in ("glBindFramebuffer", "glBindRenderbuffer", "glBindSampler",
                          "glEnableVertexAttribArray", "glDisableVertexAttribArray",
                          "glBindAttribLocation", "glBindFragDataLocation", "glBindFragDataLocationIndexed",
                          "glBindImageTexture", "glBindImageTextures", "glBindBufferBase", "glBindBufferRange"):
                # These do not change the pointer definitions or texture IDs.
                pass
            elif name.startswith(("glBind", "glVertexAttrib", "glVertexArray", "glActiveTexture", "glMultiTex", "glPushAttrib", "glPopAttrib", "glNewList", "glEndList", "glCallList")):
                # New attribute-binding APIs, DSA and legacy state restoration
                # must not be mistaken for harmless intervening calls.
                invalidate(name)
            if kind:
                previous = table.get(key) if known else None
                duplicate = previous is not None and previous[0] == value
                if selected:
                    state.stats[kind + "_calls"] += 1
                    category = "same_requested_state" if duplicate else "changed_state" if previous else "unknown_prior_state"
                    state.stats[kind + "_" + category] += 1
                    if duplicate and kind == "attribute_pointer" and previous[1] != state.shader_epoch:
                        state.stats["attribute_pointer_repeat_across_shader_change"] += 1
                    if duplicate and len(state.examples) < 12:
                        state.examples.append({"call": int(call_no), "frame": state.frame, "function": name,
                                               "key": key, "requested_state": value})
                if known:
                    table[key] = (value, state.shader_epoch)
        except (KeyError, ValueError) as error:
            raise ValueError(f"Unsupported arguments at call {call_no} ({name}): {error}") from error
    if not parsed:
        raise ValueError("No parsed API calls")
    if not any(s.stats.get("texture_bind_calls", 0) + s.stats.get("attribute_pointer_calls", 0) for s in states.values()):
        raise ValueError("No target calls with a known context in the selected frame range")
    return {"schema": 1, "status": "diagnostic_only", "performance_gain": "not_measured",
            "visual_acceptance": "not_evaluated", "backend_qualification": "requires_trace_and_loaded_module_review",
            "interpretation": "Same requested state is a candidate, not a safely removable call; shared-resource visibility and GL errors are not established.",
            "parsed_calls": parsed, "start_frame": start_frame, "end_frame_exclusive": end_frame,
            "invalidations": dict(reasons), "renderer_strings": renderer,
            "contexts": {key: {"stats": dict(s.stats), "observed_swap_calls": s.frame, "examples": s.examples} for key, s in states.items()}}


def prepare(args):
    paths = {key: Path(getattr(args, key)).resolve(strict=True) for key in ("apitrace", "wrapper", "viewer", "mesa", "settings")}
    paths["gallium"] = paths["mesa"].with_name("libgallium_wgl.dll").resolve(strict=True)
    if paths["wrapper"] == paths["mesa"]:
        raise ValueError("Tracing wrapper cannot be the Mesa implementation")
    expected_wrapper = paths["apitrace"].parent.parent / "lib" / "wrappers" / "opengl32.dll"
    if paths["wrapper"] != expected_wrapper.resolve(strict=True):
        raise ValueError("Wrapper must be the selected apitrace distribution's lib/wrappers/opengl32.dll")
    if b"APITRACE_OPENGL_DLL" not in paths["wrapper"].read_bytes() and "APITRACE_OPENGL_DLL".encode("utf-16-le") not in paths["wrapper"].read_bytes():
        raise ValueError("Wrapper lacks the explicit Mesa loader override")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    settings = output / "settings-apitrace.xml"
    shutil.copy2(paths["settings"], settings)
    plan = {"schema": 1, "status": "prepared_not_captured", "backend_qualified": False,
            "files": {k: {"path": str(p), "sha256": digest(p)} for k, p in paths.items()},
            "settings_copy_sha256": digest(settings),
            "command": [str(paths["apitrace"]), "trace", "--api=gl", "--output=" + str(output / "viewer.trace"),
                        str(paths["viewer"]), "--settings", str(settings), "--set", "RenderBackend", "Zink"],
            "environment": {"APITRACE_OPENGL_DLL": str(paths["mesa"]), "GALLIUM_DRIVER": "zink", "MESA_LOADER_DRIVER_OVERRIDE": "zink"},
            "notes": "Requires patched apitrace wrapper in its resolved wrappers directory. Verify loaded DLL paths and trace renderer; do not benchmark captured FPS. Close viewer normally to finish."}
    write_json(output / "capture-plan.json", plan)
    print(output / "capture-plan.json")


def capture(args):
    path = Path(args.plan).resolve(strict=True)
    plan = json.loads(path.read_text(encoding="utf-8"))
    for entry in plan["files"].values():
        if digest(entry["path"]) != entry["sha256"]:
            raise ValueError("Input changed since preparation: " + entry["path"])
    output = path.parent
    if digest(output / "settings-apitrace.xml") != plan["settings_copy_sha256"]:
        raise ValueError("Isolated settings changed since preparation")
    if (output / "viewer.trace").exists():
        raise ValueError("Refusing to overwrite a trace")
    env = os.environ.copy()
    env.update(plan["environment"])
    # Log creation is exclusive so a failed/partial capture cannot be rerun over.
    with open(output / "capture.log", "x", encoding="utf-8") as log:
        completed = subprocess.run(plan["command"], cwd=Path(plan["files"]["viewer"]["path"]).parent,
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
    trace = output / "viewer.trace"
    result = {"exit_code": completed.returncode, "trace_exists": trace.is_file(),
              "trace_bytes": trace.stat().st_size if trace.is_file() else 0,
              "backend_qualified": False, "status": "needs_offline_analysis_and_backend_review"}
    if completed.returncode or not result["trace_bytes"]:
        result["status"] = "capture_failed_or_abnormal_exit_partial_trace_may_be_readable"
    write_json(output / "capture-result.json", result)
    if completed.returncode or not result["trace_bytes"]:
        raise ValueError("Capture failed or empty; inspect capture.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare", help="write a capture plan; never launches viewer")
    for key in ("apitrace", "wrapper", "viewer", "mesa", "settings", "output"):
        p.add_argument("--" + key, required=True)
    p = commands.add_parser("capture", help="explicitly launch the prepared diagnostic session")
    p.add_argument("--plan", required=True)
    p = commands.add_parser("analyze", help="stream a full apitrace dump and reconstruct state offline")
    p.add_argument("--dump", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--start-frame", type=int, default=0)
    p.add_argument("--end-frame", type=int)
    p = commands.add_parser("dump", help="export trace without replay; preserve thread IDs and all GL calls")
    p.add_argument("--apitrace", required=True)
    p.add_argument("--trace", required=True)
    p.add_argument("--output", required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args)
    elif args.command == "capture":
        capture(args)
    elif args.command == "dump":
        with open(args.output, "xb") as output:
            subprocess.run([args.apitrace, "dump", "--color=never", "--thread-ids=yes", "--arg-names=yes",
                            "--multiline=no", "--verbose", "--grep=" + DUMP_FILTER, args.trace], stdout=output, check=True)
    else:
        if args.start_frame < 0 or (args.end_frame is not None and args.end_frame <= args.start_frame):
            raise ValueError("Require 0 <= start-frame < end-frame")
        with open(args.dump, encoding="utf-8-sig") as stream:
            result = analyze(stream, args.start_frame, args.end_frame)
        result["dump_sha256"] = digest(args.dump)
        write_json(args.output, result)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("error:", error, file=sys.stderr)
        sys.exit(1)
