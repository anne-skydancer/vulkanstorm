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
add_executable(smoke main.cpp)
target_compile_features(smoke PRIVATE cxx_std_20)
target_compile_options(smoke PRIVATE -Wall -Wextra -Werror)
target_link_libraries(smoke PRIVATE Threads::Threads)
enable_testing()
add_test(NAME native_thread COMMAND smoke)
EOF
cat > "$work/main.cpp" <<'EOF'
#include <atomic>
#include <thread>
int main() {
    std::atomic<int> value{0};
    std::jthread worker([&] { value.store(42); });
    worker.join();
    return value.load() == 42 ? 0 : 1;
}
EOF
cmake -S "$work" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$evidence/configure.txt" 2>&1
cmake --build "$work/build" > "$evidence/build.txt" 2>&1
ctest --test-dir "$work/build" --output-on-failure > "$evidence/ctest.txt" 2>&1
file "$work/build/smoke" > "$evidence/binary.txt"

# Inventory the actual viewer dependency gap without substituting Linux binaries
# or presenting this toolchain test as a successful viewer build.
/opt/vulkanstorm/bin/python scripts/ci/freebsd/dependency_audit.py \
    > "$evidence/viewer-dependency-audit.json"
printf '%s\n' 'FreeBSD image toolchain qualification passed' > "$evidence/result.txt"
