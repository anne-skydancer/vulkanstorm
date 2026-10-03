"""Reconcile current tracked render candidates against reviewed insertion evidence.

Discovery is deliberately broader than GL calls. A mapped candidate is not a
reviewed contract. --accept rejects unresolved source paths; runtime parity is
never certified by this tool. --write refreshes audit artifacts only.
"""
import argparse
import collections
import csv
import hashlib
import json
import pathlib
import re
import subprocess

from inventory_helpers import mask_source

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "doc/vulkan"
CPP = (".cpp", ".h", ".mm", ".c", ".hpp")
TOKENS = re.compile(
    r"\b(?:gGL\w*|gPipeline|LL(?:Render\w*|VertexBuffer\w*|ImageGL\w*|GL\w*|"
    r"TexUnit|Texture\w*|CubeMap\w*|FontGL|FontVertexBuffer|FontBitmapCache|"
    r"UIImage|ViewerDynamicTexture|ViewerTexture\w*|ViewerMedia\w*|DrawPool\w*|"
    r"DrawInfo|SpatialGroup|Drawable|Face|CullResult|Pipeline|ViewerWindow|"
    r"ScreenClipRect|LocalClipRect|GLTFSceneManager))\b"
)
VIRTUAL_ROOT = re.compile(
    r"\b(?:draw|render|preRender|postRender|destroyGL|restoreGL|shutdownGL|"
    r"updateGL|createContext|switchContext|swapBuffers|simpleSnapshot|rawSnapshot)\s*\("
)
PLATFORM_GL = re.compile(r"\b(?:wgl\w+|glX\w+|SDL_GL_\w+|GLH_EXT_GET_PROC_ADDRESS|GLH_EXT_GET_PROC_ADDRESS_CORE|GL_\w+|GLenum|GLuint|GLint|GLfloat|GLsync|LLGLuint|LLGLenum)\b")
BOUNDARY_CALL = re.compile(r"\b(?:rawSnapshot|saveSnapshot|renderSelections|setup2DRender|readBackRaw|createGLTexture|setSubImage|getGLTexture|getTexName|setTexName|releaseGLBuffers|createGLBuffers|requestResize\w*|setting_setup_signal_listener|setRenderDebugFeatureControl)\s*\(")


def tracked():
    return subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()


def discover(paths):
    commands = set((OUT / "gl-api-symbols.txt").read_text().splitlines())
    helper_header = mask_source((ROOT / "indra/llrender/llrender2dutils.h").read_text(), True)
    ui_helpers = set(re.findall(r"\b(gl_\w+)\s*\(", helper_header))
    sites = []
    for path in paths:
        p = ROOT / path
        if path.endswith(CPP):
            text = p.read_text(encoding="utf-8", errors="replace")
            clean = mask_source(text, literals=True)
            for line, (raw, code) in enumerate(zip(text.splitlines(), clean.splitlines()), 1):
                symbols = set(TOKENS.findall(code))
                symbols.update(set(re.findall(r"\bgl[A-Z]\w*(?=\s*\()", code)) & commands)
                if VIRTUAL_ROOT.search(code):
                    symbols.add("render-callback-or-lifecycle")
                symbols.update(PLATFORM_GL.findall(code))
                symbols.update(re.findall(r"\b(?:LL_PROFILE_GPU\w*|LL_PROFILER_GPU\w*|LL_PROFILER_ENABLE_TRACY_OPENGL|TracyGpu\w*)\b", code))
                if BOUNDARY_CALL.search(code):
                    symbols.add("indirect-render-boundary")
                symbols.update(set(re.findall(r"\b(gl_\w+)\s*\(", code)) & ui_helpers)
                symbols.update(re.findall(r"\b(?:uniform(?:Matrix)?\w+|drawRangeFast|drawRange|drawArrays|drawElements|setBuffer)\s*(?=\()", code))
                if re.match(r"^\s*#[ \t]*include", code) and re.search(r"#[ \t]*include[ \t]*[<\"](?:ll(?:render|gl|font|texture|imagegl|vertexbuffer|uiimage|view|drawpool|pipeline|window)|pipeline)[^>\"]*[>\"]", raw):
                    symbols.add("renderer-interface-include")
                if symbols:
                    sites.append((path, line, "cpp", ";".join(sorted(symbols)), raw.strip()))
        elif path.endswith(".glsl"):
            sites.append((path, 1, "shader", "shader-module", "module/interface/variant audit required"))
        elif path.endswith(("CMakeLists.txt", ".cmake", "autobuild.xml", "viewer_manifest.py")):
            text = p.read_text(encoding="utf-8", errors="replace")
            for line, code in enumerate(text.splitlines(), 1):
                if re.search(r"DILIGENT|diligent|VULKAN|vulkan|OPENGL|OpenGL|GLH|SDL|MESA|Mesa|Zink|llrender|llwindow|shader|GL_|USE_SDL1|BUILD_HEADLESS", code):
                    sites.append((path, line, "build", "build-gate", code.strip()))
        elif path.endswith("settings.xml") or "featuretable" in path or "panel_preferences_graphics" in path:
            text = p.read_text(encoding="utf-8", errors="replace")
            for line, code in enumerate(text.splitlines(), 1):
                if re.search(r"RenderBackend|render_backend|Vulkan|Zink|RenderGL|RenderOIT|RenderDeferred|RenderReflection|RenderMirror", code):
                    sites.append((path, line, "config", "capability-setting", code.strip()))
    return sites


def function_ranges(path):
    """Locate simple out-of-class definitions, never claim a C++ parse.

    Complex macro-generated definitions and inline members are deliberately
    unresolved context rather than attributed to the previous function.
    """
    clean = mask_source((ROOT / path).read_text(encoding="utf8", errors="replace"), True)
    pattern = re.compile(r"^[ \t]*(?:[\w:<>,*&~]+[ \t]+){0,6}([A-Za-z_]\w*(?:::[A-Za-z_~]\w*)+)\s*\([^;{}]*\)[^;{}]*\{", re.M)
    result = []
    for match in pattern.finditer(clean):
        depth = 1
        end = match.end()
        while end < len(clean) and depth:
            if clean[end] == "{": depth += 1
            elif clean[end] == "}": depth -= 1
            end += 1
        result.append((clean.count("\n", 0, match.start())+1,
                       clean.count("\n", 0, end)+1, match.group(1)))
    return result


def boundary_proof(path, kind, symbols, code, owner_records, ui_classes, enclosing_widget, global_receivers):
    """Account for insertion locations using reviewed API-boundary contracts.

    This does not mark whole files reviewed or certify algorithm parity. Every
    recognized expression/type dependency maps to an owned seam; unknown tokens
    and unowned callbacks fail. Explicit range reviews override these proofs.
    """
    ids = set(owner_records) - {"I24", "I25"}
    if kind == "shader":
        return "mapped-module", {"I06"}, "Module is a shader-assembly/ABI migration input; I06 owns all stages, linked feature closure and reflection. This is not compiled permutation or arithmetic qualification."
    if kind in ("build", "config"):
        if not ids:
            return None
        return "mapped-gate", ids, "Actual build/config expression identifies dependency, compile/staging or renderer/capability-setting gate; owned by I23/I01 and platform consumer where applicable. Runtime availability is separate."
    # Includes cannot execute a draw. Preserve their ABI dependency rather than
    # declaring the containing function/file CPU-only.
    if re.match(r'^\s*#[ \t]*include[ \t]*[<"]', code):
        return "interface-only", ids or {"I27"}, "Include statement only; migrate interface at its definition/owned facade, no executable graphics operation in this statement. Other statements/callbacks in this file are separately inventoried."
    tokens = symbols.split(";")
    unresolved = []
    routes = set()
    reasons = []
    for token in tokens:
        if token == "renderer-interface-include":
            continue
        if token.startswith(("LL_PROFILE_GPU", "LL_PROFILER_GPU", "TracyGpu")) or token == "LL_PROFILER_ENABLE_TRACY_OPENGL":
            routes.add("I21")
        elif token.startswith("gl_"):
            routes.add("I08")  # exact free-helper names came from reviewed header
        elif token.startswith(("uniform",)):
            receiver = re.search(r"(\w+)\s*(?:->|\.)\s*"+re.escape(token)+r"\s*\(", code)
            if "LLGLSLShader::" in code or (receiver and receiver.group(1) in global_receivers):
                routes.add("I06")
            elif "I06" in ids or "I13" in ids or "I17" in ids:
                # Explicit owned shader producers are catalog exception roots,
                # not arbitrary ordinary callers accepted by method spelling.
                routes.add("I06")
            else: unresolved.append(token)
        elif token in ("drawRangeFast", "drawRange", "drawArrays", "drawElements", "setBuffer"):
            if "LLVertexBuffer::" in code or "I04" in ids or "I09" in ids or "I13" in ids or "I22" in ids:
                routes.add("I04")
            else: unresolved.append(token)
        elif re.fullmatch(r"(?:wgl\w+|glX\w+|SDL_GL_\w+)", token):
            routes.add("I02")
        elif token.startswith("GLH_EXT_GET_PROC_ADDRESS"):
            routes.add("I01")
        elif token.startswith("gl") and token[2:3].isupper():
            # A raw API escape is itself an insertion point, not a facade pass-
            # through. Keep the semantic subsystem owner PLUS its operation seam.
            if not ids:
                unresolved.append(token)
                continue
            if re.search(r"Shader|Program|Uniform|AttribLocation", token): routes.add("I06")
            elif re.search(r"Query", token): routes.add("I21")
            elif re.search(r"ReadPixels|GetTexImage|GetCompressedTexImage|GetBufferSubData", token): routes.add("I19")
            elif re.search(r"Fence|WaitSync|Finish|Flush|MemoryBarrier", token): routes.add("I05")
            elif re.search(r"Framebuffer|Renderbuffer|DrawBuffer|ReadBuffer|Viewport|Clear", token): routes.add("I07")
            elif re.search(r"Tex|Texture|PixelStore|Mipmap|Image", token): routes.add("I05")
            elif re.search(r"Buffer|Vertex|DrawArrays|DrawElements|DrawRange", token): routes.add("I04")
            elif re.search(r"GetError|DebugMessage|GetString|GetInteger|GetFloat|GetBoolean|IsEnabled|ObjectLabel|Hint", token): routes.add("I01")
            else: routes.add("I03")
        elif token.startswith(("GL_", "GLenum", "GLuint", "GLint", "GLfloat", "GLsync", "LLGLuint", "LLGLenum")):
            if not ids:
                unresolved.append(token)
            else:
                routes.add("I03")
        elif token == "indirect-render-boundary":
            if re.search(r"Snapshot|snapshot", code): routes.add("I19")
            elif re.search(r"GLTexture|TexName|readBackRaw|setSubImage", code): routes.add("I05")
            elif re.search(r"setting_setup_signal_listener", code): routes.add("I01")
            elif ids: routes.update(ids)
            else: unresolved.append(token)
        elif token == "render-callback-or-lifecycle":
            match = re.search(r"\b([A-Za-z_]\w*)::(?:draw|render|preRender|postRender|destroyGL|restoreGL|shutdownGL|updateGL|createContext|switchContext|swapBuffers)\s*\(", code)
            if match and match.group(1) in ui_classes:
                routes.add("I08")
                reasons.append("Callback owner "+match.group(1)+" belongs to the verified LLView/LLTextSegment inheritance closure; preserve virtual widget/text-segment traversal and lower its draws through I08/I09.")
            elif enclosing_widget:
                routes.add("I08")
                reasons.append("Callback declaration/expression is inside verified widget/text-segment inheritance owner "+enclosing_widget+"; shared widget draw interface remains, renderer emissions are separately indexed.")
            elif ids:
                routes.update(ids)
            else:
                unresolved.append(token)
        elif token.startswith(("LLFont",)):
            routes.add("I09")
        elif token.startswith(("LLUIImage", "LLTextureCtrl", "LLTextureView", "LLTextureBar", "LLTextureToolTip", "LLTexturePreviewView")):
            routes.add("I08")
        elif token.startswith(("LLViewerMedia",)):
            routes.add("I10")
        elif token.startswith(("LLGLTF",)):
            routes.add("I13")
        elif token.startswith(("LLTextureKey", "LLTexturePipelineTester", "LLTextureTestSession", "LLTextureEntry", "LLTextureAnim", "LLTextureCache", "LLTextureFetch", "LLTextureInfo", "LLTextureStats", "LLRenderMuteList", "LLRenderMaterialParams")):
            routes.add("I26")
        elif token.startswith(("LLTextureBridge", "LLTextureUploadData", "LLTextureMaskData", "LLViewerTexture", "LLGLTexture", "LLImageGL", "LLTextureManagerBridge")) or token == "LLTexture":
            routes.add("I05")
        elif token.startswith(("LLVertexBuffer",)) or token == "LLDrawInfo":
            routes.add("I04")
        elif token.startswith(("LLCubeMap", "LLRenderTarget")):
            routes.add("I07")
        elif token in ("LLDrawable", "LLFace", "LLSpatialGroup", "LLCullResult"):
            routes.add("I11")
        elif token in ("LLViewerWindow",):
            routes.add("I02")
        elif token in ("LLScreenClipRect", "LLLocalClipRect"):
            routes.add("I08")
        elif token.startswith(("LLGL",)):
            routes.add("I03")
        elif token.startswith(("gGL",)):
            routes.add("I01" if token == "gGLManager" else "I03")
        elif token.startswith(("LLDrawPool",)):
            routes.add("I13")
        elif token in ("LLPipeline", "gPipeline"):
            routes.add("I12")
        elif token.startswith(("LLRender",)) or token == "LLTexUnit":
            routes.add("I03")
        elif token == "LLViewerDynamicTexture":
            routes.add("I18")
        else:
            unresolved.append(token)
    if unresolved or not routes:
        return None
    witnesses = "; ".join(tokens)
    return "mapped-boundary", ids | routes, "Witness "+witnesses+" -> "+",".join(sorted(routes))+"; owned responsibilities/public APIs are defined in those records. "+" ".join(reasons)+" Raw GL is an explicit rewrite point; font API/type dependencies include lazy atlas uploads, never a whole-function CPU exclusion."


def widget_classes(paths):
    parents = {}
    for path in paths:
        if not path.endswith(CPP): continue
        code = mask_source((ROOT/path).read_text(encoding="utf8", errors="replace"), True)
        for child, bases in re.findall(r"\bclass\s+(\w+)\s*(?:final\s*)?:\s*([^;{]+)\{", code):
            parents.setdefault(child, set()).update(re.findall(r"\b[A-Za-z_]\w*\b", bases))
    classes = {"LLView", "LLTextSegment"}
    while True:
        added = {child for child,bases in parents.items() if bases & classes} - classes
        if not added: return classes
        classes.update(added)


def class_ranges(path):
    clean = mask_source((ROOT/path).read_text(encoding="utf8", errors="replace"), True)
    result = []
    for match in re.finditer(r"\bclass\s+(\w+)\s*(?:final\s*)?(?::[^;{]+)?\{", clean):
        depth=1; end=match.end()
        while end<len(clean) and depth:
            if clean[end]=="{": depth+=1
            elif clean[end]=="}": depth-=1
            end+=1
        result.append((clean.count("\n",0,match.start())+1, clean.count("\n",0,end)+1, match.group(1)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--accept", action="store_true")
    args = parser.parse_args()
    records = json.loads((OUT / "diligent-insertion-records.json").read_text())
    source_commit = records["source_commit"]
    scope_paths = ["indra", "3p", "autobuild.xml", "scripts/build_vulkan_dependencies.py"]
    delta = subprocess.check_output(["git", "diff", "--name-only", source_commit, "--", *scope_paths], cwd=ROOT, text=True)
    if delta:
        raise SystemExit(f"Renderer source differs from catalog baseline; review and update source pin: {delta}")
    by_id = {r["id"]: r for r in records["records"]}
    if len(by_id) != len(records["records"]):
        raise SystemExit("Duplicate insertion record")
    for record in by_id.values():
        for evidence in record.get("evidence", []):
            path = ROOT / evidence["path"]
            text = path.read_text(encoding="utf-8", errors="replace")
            if evidence["symbol"] not in text:
                raise SystemExit(f"Stale evidence: {record['id']} {evidence}")
    paths = tracked()
    sites = []
    review_ranges = records.get("reviewed_ranges", [])
    allowed_dispositions = {"mapped", "cpu-only", "dormant", "test-only", "platform-out-of-scope",
                            "mapped-module", "mapped-gate", "interface-only", "mapped-boundary",
                            "conditional", "conditional-mapped", "unmapped", "needs-source-review"}
    for reviewed in review_ranges:
        if reviewed["disposition"] not in allowed_dispositions or reviewed["record"] not in by_id:
            raise SystemExit(f"Unknown review disposition/record: {reviewed}")
        if hashlib.sha256((ROOT/reviewed["path"]).read_bytes()).hexdigest() != reviewed["sha256"]:
            raise SystemExit(f"Stale reviewed source: {reviewed['path']}")
    ui_classes = widget_classes(paths)
    global_receivers = set(re.findall(r"\bextern\s+LLGLSLShader\s+(g\w+)\b", mask_source((ROOT/"indra/newview/llviewershadermgr.h").read_text(), True)))
    contexts = {}
    class_contexts = {}
    for path, line, kind, symbols, code in discover(paths):
        matches = [r for r in by_id.values() if any(re.search(pattern, path) for pattern in r["paths"])
                   or any(re.search(pattern, symbols) for pattern in r.get("symbol_patterns", []))]
        ids = ";".join(r["id"] for r in matches)
        disposition = "needs-source-review" if matches else "unmapped"
        evidence = ""
        if kind == "cpp" and path not in contexts:
            contexts[path] = function_ranges(path)
            class_contexts[path] = class_ranges(path)
        function = next((name for first,last,name in reversed(contexts.get(path, [])) if first <= line <= last), "declaration/macro/inline-or-unresolved-context")
        enclosing_widget = next((name for first,last,name in class_contexts.get(path, []) if first <= line <= last and name in ui_classes), "")
        if not enclosing_widget and function.split("::")[0] in ui_classes:
            enclosing_widget = function.split("::")[0]
        proof = boundary_proof(path,kind,symbols,code,[r["id"] for r in matches],ui_classes,enclosing_widget,global_receivers)
        if proof:
            disposition, routed, evidence = proof
            ids = ";".join(sorted(routed))
        for reviewed in review_ranges:
            if reviewed["path"] == path and reviewed["start"] <= line <= reviewed["end"]:
                digest = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                if digest != reviewed["sha256"]:
                    raise SystemExit(f"Stale reviewed source: {path}")
                if reviewed["record"] not in by_id:
                    raise SystemExit(f"Unknown reviewed record: {reviewed}")
                ids = reviewed["record"]
                disposition = reviewed["disposition"]
                evidence = reviewed["reason"]
                break
        sites.append(dict(path=path, line=line, kind=kind, function=function, symbols=symbols, records=ids,
                          disposition=disposition, evidence=evidence, source=code))
    # Existing lexical candidates must remain represented even if discovery changes.
    old = {r["path"] for r in csv.DictReader((OUT / "gl-source-inventory.csv").open(encoding="utf8"))}
    discovered = {s["path"] for s in sites}
    missing = sorted(old - discovered)
    if missing:
        raise SystemExit(f"Previous GL candidates not reconciled: {missing}")
    previous_rows = list(csv.DictReader((OUT / "shader-registration.csv").open(encoding="utf8")))
    contract_ids = {r["id"] for r in csv.DictReader((OUT / "coverage-ledger.csv").open(encoding="utf8"))}
    mapped_contracts = {c for r in by_id.values() for c in r.get("contracts", [])}
    if contract_ids - mapped_contracts:
        raise SystemExit(f"Original contracts omitted: {sorted(contract_ids-mapped_contracts)}")
    shader_mgr = (ROOT / "indra/newview/llviewershadermgr.cpp").read_text().splitlines()
    for row in previous_rows:
        if shader_mgr[int(row["line"]) - 1].strip() != row["registration_or_variant"]:
            raise SystemExit(f"Stale shader registration at {row['line']}")
    status = collections.Counter(s["disposition"] for s in sites)
    if set(status) - allowed_dispositions:
        raise SystemExit(f"Unknown generated disposition: {set(status)-allowed_dispositions}")
    summary = dict(source_commit=source_commit,
                   tracked_source_paths_scanned=sum(p.startswith(("indra/", "3p/")) or p == "autobuild.xml" for p in paths), candidate_files=len(discovered), candidate_sites=len(sites),
                   original_GL_candidate_files=len(old), additional_candidate_files=len(discovered-old),
                   shader_registration_rows_verified=len(previous_rows), dispositions=dict(status),
                   source_coverage_accepted=not any(s["disposition"] in ("unmapped", "needs-source-review") for s in sites),
                   runtime_parity_qualified=False)
    if args.write:
        with (OUT / "diligent-insertion-sites.csv").open("w", encoding="utf8", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=list(sites[0]))
            writer.writeheader()
            writer.writerows(sites)
        (OUT / "diligent-insertion-summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    else:
        with (OUT / "diligent-insertion-sites.csv").open(encoding="utf8", newline="") as saved:
            if json.loads((OUT / "diligent-insertion-summary.json").read_text()) != summary:
                raise SystemExit("Insertion summary artifact is stale")
            if list(csv.DictReader(saved)) != [{k: str(v) for k,v in s.items()} for s in sites]:
                raise SystemExit("Insertion-site artifact is stale; inspect source delta then regenerate with --write")
    print(json.dumps(summary, indent=2))
    if args.accept and not summary["source_coverage_accepted"]:
        raise SystemExit("Source completeness rejected: unreviewed/unmapped paths remain. Mechanical mapping is not semantic acceptance.")


if __name__ == "__main__":
    main()
