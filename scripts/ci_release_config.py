"""Validate the configured release against the locally qualified feature set."""
import pathlib
import sys

cache = pathlib.Path(sys.argv[1])
windows = sys.argv[2] == "Windows"
development = len(sys.argv) > 3 and sys.argv[3] == "--development"
values = {}
for line in cache.read_text(encoding="utf-8").splitlines():
    if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value

enabled = {"PACKAGE", "USE_SOLOUD", "USE_AVX2_OPTIMIZATION", "USE_LTO",
           "USE_PRECOMPILED_HEADERS"}
disabled = {"OPENSIM", "USE_KDU", "INSTALL_PROPRIETARY", "USE_OPENAL",
            "USE_FMODSTUDIO", "USE_DISCORD", "USE_BUGSPLAT", "USE_TRACY",
            "USE_AVX_OPTIMIZATION", "USE_VELOPACK", "USE_NSIS", "LL_TESTS",
            "RELEASE_CRASH_REPORTING", "NON_RELEASE_CRASH_REPORTING"}
enabled.add("USE_MESAZINK")
(enabled if windows else disabled).add("USE_INNOSETUP")
if development:
    enabled -= {"PACKAGE", "USE_INNOSETUP"}
    disabled |= {"PACKAGE", "USE_INNOSETUP"}
errors = []
for key in sorted(enabled | disabled):
    actual = values.get(key, "<missing>").upper()
    valid = {"ON", "TRUE", "1", "YES"} if key in enabled else {"OFF", "FALSE", "0", "NO"}
    if actual not in valid:
        errors.append(f"{key}: expected {'ON' if key in enabled else 'OFF'}, got {actual}")
for key, expected in {"CMAKE_BUILD_TYPE": "RelWithDebInfo" if development else "Release", "ADDRESS_SIZE": "64",
                      "VIEWER_CHANNEL": "Vulkanstorm-Release", "BUGSPLAT_DB": ""}.items():
    if values.get(key) != expected:
        errors.append(f"{key}: expected {expected}, got {values.get(key)}")
if errors:
    raise SystemExit("Release configuration mismatch:\n" + "\n".join(errors))
label = "Development" if development else "Release"
print(f"{label} configuration matches the Release feature set ({sys.argv[2]}).")
