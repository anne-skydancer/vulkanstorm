"""Reject development harnesses in release runtime sources (run for master only).

Standalone profiling tools and normal viewer debug facilities remain permitted.
This is a branch policy check, not a rendering correctness test.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN = re.compile(
    r"\b(?:VULKANSTORM_(?:ADV_DEBUG|CAPTURE|CLEAR_TEAL|LIST_DEBUG|MENU_DEBUG|"
    r"NO_BORDER|NO_CLIPSTACK|NO_LINEEDIT|NO_MENU|NO_NEWVIEW_HOOKS|NO_WIDGETS|"
    r"TEXT_DEBUG|TREE_DUMP|UI_DEBUG|UITEST)|"
    r"LLVKUITestScene|readbackSwapchain|gl_render_ui_test_scene|gl_capture_frame_once|"
    r"FocusRenderProbe|RenderAlphaOITProfile|OITProfileSample|"
    r"sCompletionPending|sampleSourceMemory|accumulateSourceMemory|"
    r"getOwnedRawBytes|getPeakOwnedRawBytes|getOwnedOtherImageBytes|getDetachedRawBytes|"
    r"getCPUVertexBytes|getCPUIndexBytes|USE_TRACY_MEMORY)\b"
)


def main():
    failures = []
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "--", "indra"], cwd=ROOT
    ).decode("utf-8").split("\0")
    for name in sorted(filter(None, tracked)):
        path = ROOT / name
        if path.suffix not in {".cpp", ".h", ".mm", ".cmake"} and path.name not in {"settings.xml", "CMakeLists.txt"}:
            continue
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT)
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            match = FORBIDDEN.search(line)
            if match:
                failures.append(f"{relative}:{number}: development hook {match.group()}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("Release runtime development-hook policy passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
