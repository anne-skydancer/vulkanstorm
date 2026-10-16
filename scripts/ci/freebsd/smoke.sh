#!/bin/sh
# Runs as the unprivileged runner in a fresh boot of the completed image.
set -eu

[ "$(uname -s)" = FreeBSD ]
[ "$(uname -m)" = amd64 ]
[ "$(id -u)" -ne 0 ]
case "$(freebsd-version -u)" in 15.0-RELEASE*) ;; *) exit 1 ;; esac

evidence="$PWD/freebsd-image-evidence"
mkdir -p "$evidence"
cp /usr/local/share/vulkanstorm-image/* "$evidence/"
uname -a > "$evidence/uname.txt"
autobuild --version > "$evidence/autobuild.txt" 2>&1
/opt/vulkanstorm/bin/python scripts/ci/freebsd/test_autobuild_platform.py \
    > "$evidence/autobuild-platform-tests.txt" 2>&1
/opt/vulkanstorm/bin/python -c 'from autobuild import common; assert common.get_current_platform() == "freebsd64"'
/opt/vulkanstorm/bin/python -c 'import llsd; assert llsd.parse(llsd.format_xml({"native": True}))["native"]'

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(freebsd_image_smoke LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  message(FATAL_ERROR "Expected native FreeBSD")
endif()
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
add_executable(smoke main.cpp)
target_compile_features(smoke PRIVATE cxx_std_20)
target_compile_options(smoke PRIVATE -Wall -Wextra -Werror)
target_link_libraries(smoke PRIVATE Threads::Threads PkgConfig::SDL2 PkgConfig::PNG PkgConfig::CURL)
enable_testing()
add_test(NAME native_thread COMMAND smoke)
EOF
cat > "$work/main.cpp" <<'EOF'
#include <atomic>
#include <span>
#include <thread>
#include <SDL.h>
#include <png.h>
#include <curl/curl.h>
int main() {
    SDL_version sdl{};
    SDL_GetVersion(&sdl);
    if (sdl.major != 2 || png_access_version_number() == 0 || !curl_version()) return 2;
    std::atomic<int> value{0};
    int input[] = {42};
    std::span<const int> values(input);
    std::thread worker([&] { value.store(values.front()); });
    worker.join();
    return value.load() == 42 ? 0 : 1;
}
EOF
cmake -S "$work" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$evidence/configure.txt" 2>&1
cmake --build "$work/build" > "$evidence/build.txt" 2>&1
ctest --test-dir "$work/build" --output-on-failure > "$evidence/ctest.txt" 2>&1
file "$work/build/smoke" > "$evidence/binary.txt"
ldd "$work/build/smoke" > "$evidence/linked-libraries.txt"
pkg-config --modversion sdl2 libpng libcurl > "$evidence/system-library-versions.txt"

# Inventory the actual viewer dependency gap without substituting Linux binaries
# or presenting this toolchain test as a successful viewer build.
/opt/vulkanstorm/bin/python scripts/ci/freebsd/dependency_audit.py \
    > "$evidence/viewer-dependency-audit.json"
printf '%s\n' 'FreeBSD image toolchain qualification passed' > "$evidence/result.txt"
