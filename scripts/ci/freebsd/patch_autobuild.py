"""Add native FreeBSD platform identity to the image's pinned Autobuild 3.9.3.

This does not map FreeBSD to Linux, install viewer dependencies, or modify
the pkg-managed Python environment. Fail if upstream code no longer matches.
"""

from importlib.metadata import distribution


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Autobuild patch context mismatch: {old!r}")
    return source.replace(old, new, 1)


def patch_common(source):
    source = replace_once(source, "PLATFORM_COMMON    = 'common'",
                          "PLATFORM_FREEBSD64 = 'freebsd64'\nPLATFORM_COMMON    = 'common'")
    source = replace_once(source, "DEFAULT_ADDRSIZE = 32",
                          "DEFAULT_ADDRSIZE = 64 if sys.platform.startswith('freebsd') else 32")
    source = replace_once(source, "    elif platform != PLATFORM_COMMON:",
                          "    elif platform == PLATFORM_FREEBSD64:\n"
                          "        if not sys.platform.startswith('freebsd'):\n"
                          "            platform_should_be = 'FreeBSD'\n"
                          "    elif platform != PLATFORM_COMMON:")
    source = replace_once(source, "    elif sys.platform.startswith('linux'):\n",
                        "    elif sys.platform.startswith('freebsd'):\n"
                        "        if addrsize != 64:\n"
                        "            raise AutobuildError('FreeBSD image requires a 64-bit target')\n"
                        "        Platform = PLATFORM_FREEBSD64\n"
                        "    elif sys.platform.startswith('linux'):\n")
    return replace_once(source, "    check_platform_system_match(Platform)",
                        "    if Platform == PLATFORM_FREEBSD64 and addrsize != 64:\n"
                        "        raise AutobuildError('FreeBSD image requires a 64-bit target')\n"
                        "    check_platform_system_match(Platform)")


def patch_environment(source):
    source = replace_once(source, '                linux2="LINUX",',
                          '                linux2="LINUX",\n                freebsd="FREEBSD",')
    return replace_once(source, '                )[sys.platform]',
                        "                )['freebsd' if sys.platform.startswith('freebsd') else sys.platform]")


if __name__ == "__main__":
    package = distribution("autobuild")
    if package.version != "3.9.3":
        raise RuntimeError(f"Expected Autobuild 3.9.3, found {package.version}")
    # Validate both transformations before writing either file.
    changes = []
    for name, transform in (("common.py", patch_common),
                            ("autobuild_tool_source_environment.py", patch_environment)):
        path = package.locate_file(f"autobuild/{name}")
        result = transform(path.read_text())
        compile(result, str(path), "exec")
        changes.append((path, result))
    for path, result in changes:
        path.write_text(result)
