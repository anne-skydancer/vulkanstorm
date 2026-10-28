#!/usr/bin/env bash
# Report the newest glibc (GLIBC_) and libstdc++ (GLIBCXX_, CXXABI_) symbol
# versions that the built Linux viewer and its shared libraries require.
# usage: ci_report_glibc.sh <build-directory>/newview
# Writes a table to stdout and, in GitHub Actions, to the step summary.
# With VKSTORM_MAX_GLIBC and/or VKSTORM_MAX_GLIBCXX set (e.g. 2.39, 3.4.33),
# exits 1 if any file needs a newer version, listing the files that do.
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
# Every executable and shared library (SLPlugin, dullahan_host, chrome-sandbox,
# the crash logger, ...); the ELF test above drops scripts. CMakeFiles holds
# only build-time probes.
done < <(find "$root" -path '*/CMakeFiles' -prune -o -type f \
              \( -perm -u+x -o -name '*.so' -o -name '*.so.*' \) -print0 2>/dev/null)

{
    echo "### Linux runtime requirements"
    echo
    echo "Newest required: GLIBC_${max_glibc:-none}, GLIBCXX_${max_glibcxx:-none}, CXXABI_${max_cxxabi:-none}"
    echo "Compiler: $(${CXX:-g++} --version | head -n 1); build glibc: $(ldd --version | head -n 1)"
    echo
    cat "$out"
} | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"

# Column 2 is GLIBC, column 3 GLIBCXX; rows look like "| `file` | 2.39 | 3.4.33 | 1.3.15 |".
too_new() { # <column> <limit> <label>
    local col=$1 limit=$2 label=$3 bad
    bad=$(awk -F' [|] ' -v c="$col" 'NR > 2 { v = $c; gsub(/[ |]/, "", v); if (v != "-") print v "\t" $1 }' "$out" |
          while IFS=$'\t' read -r v f; do
              [ "$(printf '%s\n%s\n' "$v" "$limit" | sort -V | tail -n 1)" = "$limit" ] || echo "$f ($label $v)"
          done)
    [ -z "$bad" ] && return 1
    echo "Needs a newer $label than $limit:" >&2
    sed 's/^| *//' <<<"$bad" >&2
}
status=0
if [ -n "${VKSTORM_MAX_GLIBC:-}" ] && too_new 2 "$VKSTORM_MAX_GLIBC" GLIBC; then status=1; fi
if [ -n "${VKSTORM_MAX_GLIBCXX:-}" ] && too_new 3 "$VKSTORM_MAX_GLIBCXX" GLIBCXX; then status=1; fi
rm -f "$out"
exit "$status"
