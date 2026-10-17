"""Build an extract-and-run Skin Studio bundle on Windows or Linux (Python 3.12+)."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
from urllib.request import urlopen
from zipfile import ZIP_DEFLATED, ZipFile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def native_path(path: Path) -> Path:
    path = path.resolve()
    if os.name == "nt" and not str(path).startswith("\\\\?\\"):
        text = str(path)
        return Path("\\\\?\\UNC\\" + text[2:] if text.startswith("\\\\") else "\\\\?\\" + text)
    return path


def verify_runtime(path: Path, expected: str) -> None:
    with path.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != expected:
        raise ValueError(f"Python archive SHA-256 mismatch: expected {expected}, got {actual}")


def download_verified(url: str, archive: Path, expected: str) -> None:
    if not archive.exists():
        print(f"Downloading pinned input: {url}", flush=True)
        with urlopen(url, timeout=120) as response, tempfile.NamedTemporaryFile(
            dir=archive.parent, delete=False
        ) as download:
            temporary = Path(download.name)
            try:
                shutil.copyfileobj(response, download)
            except BaseException:
                download.close()
                temporary.unlink()
                raise
        try:
            verify_runtime(temporary, expected)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    verify_runtime(archive, expected)


def copy_python_notices(archive: Path, destination: Path, release: str) -> None:
    destination.mkdir()
    prefix = f"python-build-standalone-{release}/"
    count = 0
    with tarfile.open(archive) as source:
        for entry in source.getmembers():
            if not entry.name.startswith(prefix):
                continue
            name = entry.name[len(prefix):]
            if "/" in name or not (name.startswith("LICENSE") or name == "python-licenses.rst"):
                continue
            if entry.isfile():
                stream = source.extractfile(entry)
                if stream is None:
                    raise ValueError(f"Missing Python licence contents: {name}")
                with stream:
                    (destination / name).write_bytes(stream.read())
                count += 1
    if not count:
        raise ValueError("Python dependency licence archive contains no notices.")


def unpack_runtime(archive: Path, destination: Path) -> None:
    with tarfile.open(archive) as runtime:
        for entry in runtime.getmembers():
            if entry.name != "python" and not entry.name.startswith("python/"):
                raise ValueError(f"Unexpected Python archive entry: {entry.name}")
        runtime.extractall(destination, filter="data")


def stage_assets(destination: Path, root: Path = ROOT) -> None:
    editor = destination / "bundle" / "editor"
    editor.mkdir(parents=True)
    for name in ("editor.py", "index.html", "editor.js", "editor.css", "README.md"):
        shutil.copy2(root / "tools" / "skin-editor" / name, editor / name)
    library = destination / "bundle" / "library" / "indra" / "newview"
    shutil.copytree(root / "indra" / "newview" / "skins", library / "skins",
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    (library / "app_settings").mkdir()
    shutil.copy2(root / "indra" / "newview" / "app_settings" / "commands.xml",
                 library / "app_settings" / "commands.xml")
    notices = destination / "licenses"
    notices.mkdir()
    shutil.copy2(root / "LICENSE", notices / "LICENSE")
    for name in ("LICENSE-source.txt", "LICENSE-logos.txt", "LGPL-license.txt"):
        shutil.copy2(root / "doc" / name, notices / name)
    guide = (HERE / "README.md").read_text(encoding="utf-8")
    (destination / "README.md").write_text(
        guide.replace("(../README.md)", "(bundle/editor/README.md)"), encoding="utf-8"
    )
    source = destination / "source" / "desktop"
    shutil.copytree(HERE / "src", source / "src")
    for name in ("Cargo.toml", "Cargo.lock", "package.py", "runtime-lock.json", "README.md", "bridge.test.cjs"):
        shutil.copy2(HERE / name, source / name)


def copy_crate_notices(destination: Path, metadata: dict) -> None:
    notices = destination / "licenses" / "rust"
    notices.mkdir()
    for package in metadata["packages"]:
        source = Path(package["manifest_path"]).parent
        target = notices / f"{package['name']}-{package['version']}"
        target.mkdir()
        (target / "package.json").write_text(json.dumps({
            key: package.get(key) for key in ("name", "version", "license", "repository")
        }, indent=2), encoding="utf-8")
        for path in source.iterdir():
            if path.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "COPYRIGHT")):
                if path.is_file():
                    shutil.copy2(path, target / path.name)
                elif path.is_dir():
                    shutil.copytree(path, target / path.name)
        if package.get("license_file"):
            path = source / package["license_file"]
            if path.is_file() and not (target / path.name).exists():
                shutil.copy2(path, target / path.name)


def create_archive(stage: Path, output: Path, system: str) -> None:
    if system == "windows":
        with ZipFile(output, "w", ZIP_DEFLATED, strict_timestamps=False) as archive:
            for path in stage.rglob("*"):
                archive.write(path, path.relative_to(stage.parent))
    else:
        with tarfile.open(output, "w:gz") as archive:
            archive.add(stage, arcname=stage.name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-archive", type=Path, help="Use a predownloaded, pinned Python archive")
    parser.add_argument("--output", type=Path, default=HERE / "dist")
    args = parser.parse_args()
    system = {"Windows": "windows", "Linux": "linux"}.get(platform.system())
    if system is None or platform.machine().lower() not in ("amd64", "x86_64"):
        parser.error("Build on Windows x86-64 or Linux x86-64 for the matching platform.")
    target = f"{system}-x86_64"
    lock = json.loads((HERE / "runtime-lock.json").read_text(encoding="utf-8"))
    runtime = lock[target]
    cache = HERE / "cache"
    cache.mkdir(exist_ok=True)
    archive = args.runtime_archive or cache / runtime["archive"]
    if args.runtime_archive is None:
        url = (f"https://github.com/astral-sh/python-build-standalone/releases/download/"
               f"{lock['release']}/{runtime['archive']}")
        download_verified(url, archive, runtime["sha256"])
    else:
        verify_runtime(archive, runtime["sha256"])
    notices = lock["notices"]
    notice_archive = cache / notices["archive"]
    download_verified(notices["url"], notice_archive, notices["sha256"])
    subprocess.run(["cargo", "build", "--release", "--locked"], cwd=HERE, check=True)
    metadata = json.loads(subprocess.check_output(
        ["cargo", "metadata", "--locked", "--format-version", "1"],
        cwd=HERE, text=True, encoding="utf-8"
    ))
    name = f"skin-studio-{target}"
    args.output = native_path(args.output)
    args.output.mkdir(parents=True, exist_ok=True)
    final = args.output / name
    archive_suffix = ".zip" if system == "windows" else ".tar.gz"
    if final.exists() or (args.output / f"{name}{archive_suffix}").exists():
        parser.error(f"Output already exists: {final}. Move it aside or choose another --output.")
    with tempfile.TemporaryDirectory(prefix="skin-studio-stage-", dir=args.output) as temporary:
        stage = Path(temporary) / name
        stage.mkdir()
        stage_assets(stage)
        unpack_runtime(archive, stage / "bundle")
        executable = "skin-studio.exe" if system == "windows" else "skin-studio"
        shutil.copy2(Path(metadata["target_directory"]) / "release" / executable, stage / executable)
        copy_crate_notices(stage, metadata)
        copy_python_notices(notice_archive, stage / "licenses" / "python", lock["release"])
        (stage / "runtime-lock.json").write_text(json.dumps(lock, indent=2), encoding="utf-8")
        subprocess.run([str(stage / executable), "--check"], check=True)
        result = Path(temporary) / f"{name}{archive_suffix}"
        create_archive(stage, result, system)
        result.replace(args.output / result.name)
    print(f"Standalone bundle: {args.output / (name + archive_suffix)}")


if __name__ == "__main__":
    main()
