#!/usr/bin/env bash
# Report the newest glibc (GLIBC_) and libstdc++ (GLIBCXX_, CXXABI_) symbol
# versions that the built Linux viewer and its shared libraries require.
# usage: ci_report_glibc.sh <build-directory>/newview
# Writes a table to stdout and, in GitHub Actions, to the step summary.
set -euo pipefail

root=${1:?usage: ci_report_glibc.sh <build-dir>/newview}
out=$(mktemp)
max_glibc=""; max_glibcxx=""; max_cxxabi=""
newest() { sort -V | tail -n 1; }

printf '| File | GLIBC | GLIBCXX | CXXABI |\n|---|---|---|---|\n' > "$out"
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q '^ELF' || continue
    syms=$(objdump -T "$f" 2>/dev/null || true)
    g=$(grep -o 'GLIBC_[0-9][0-9.]*' <<<"$syms" | sed 's/GLIBC_//' | newest || true)
    x=$(grep -o 'GLIBCXX_[0-9][0-9.]*' <<<"$syms" | sed 's/GLIBCXX_//' | newest || true)
    a=$(grep -o 'CXXABI_[0-9][0-9.]*' <<<"$syms" | sed 's/CXXABI_//' | newest || true)
    [ -n "$g$x$a" ] || continue
    printf '| `%s` | %s | %s | %s |\n' "${f#$root/}" "${g:--}" "${x:--}" "${a:--}" >> "$out"
    [ -n "$g" ] && max_glibc=$(printf '%s\n%s\n' "$max_glibc" "$g" | grep . | newest)
    [ -n "$x" ] && max_glibcxx=$(printf '%s\n%s\n' "$max_glibcxx" "$x" | grep . | newest)
    [ -n "$a" ] && max_cxxabi=$(printf '%s\n%s\n' "$max_cxxabi" "$a" | grep . | newest)
done < <(find "$root" -type f \( -name '*-bin' -o -name '*.so' -o -name '*.so.*' \) -print0 2>/dev/null)

{
    echo "### Linux runtime requirements"
    echo
    echo "Newest required: GLIBC_${max_glibc:-none}, GLIBCXX_${max_glibcxx:-none}, CXXABI_${max_cxxabi:-none}"
    echo "Compiler: $(${CXX:-g++} --version | head -n 1); build glibc: $(ldd --version | head -n 1)"
    echo
    cat "$out"
} | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"
rm -f "$out"
