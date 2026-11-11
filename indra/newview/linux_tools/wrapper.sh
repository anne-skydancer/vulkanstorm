#!/bin/bash

## Here are some configuration options for Linux Client Users.

## - Avoids using any OpenAL audio driver.
#export LL_BAD_OPENAL_DRIVER=x

## GL Driver Options
export mesa_glthread=true

## Everything below this line is just for advanced troubleshooters.
##-------------------------------------------------------------------

## - For advanced debugging cases, you can run the viewer under the
##   control of another program, such as strace, gdb, or valgrind.  If
##   you're building your own viewer, bear in mind that the executable
##   in the bin directory will be stripped: you should replace it with
##   an unstripped binary before you run.
#export LL_WRAPPER='gdb --args'
#export LL_WRAPPER='valgrind --smc-check=all --error-limit=no --log-file=secondlife.vg --leak-check=full --suppressions=/usr/lib/valgrind/glibc-2.5.supp --suppressions=secondlife-i686.supp'
#export ASAN_OPTIONS="halt_on_error=0 detect_leaks=1 symbolize=1"
#export UBSAN_OPTIONS="print_stacktrace=1 print_summary=1 halt_on_error=0"

## Nothing worth editing below this line.
##-------------------------------------------------------------------

SCRIPTSRC=`readlink -f "$0" || echo "$0"`
RUN_PATH=`dirname "${SCRIPTSRC}" || echo .`
echo "Running from ${RUN_PATH}"
cd "${RUN_PATH}"

# Re-register the secondlife:// protocol handler every launch, for now.
./etc/register_secondlifeprotocol.sh

# Re-register the application with the desktop system every launch, for now.
./etc/refresh_desktop_app_entry.sh

## Before we mess with LD_LIBRARY_PATH, save the old one to restore for
##  subprocesses that care.
export SAVED_LD_LIBRARY_PATH="${LD_LIBRARY_PATH}"

# Add our library directory
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"

# Copy "$@" to ARGS array specifically to delete the --skip-gridargs switch.
# The gridargs.dat file is no more, but we still want to avoid breaking
# scripts that invoke this one with --skip-gridargs.
ARGS=()
for ARG in "$@"; do
    if [ "--skip-gridargs" != "$ARG" ]; then
        ARGS[${#ARGS[*]}]="$ARG"
    fi
done

# Vulkanstorm is built for x86-64-v3 (AVX2). On an older processor the viewer
# would die with an illegal-instruction fault and no explanation, so check first.
# VKSTORM_LD_SO and VKSTORM_CPUINFO exist so that the check can be tested.
vkstorm_cpu_ok() {
	[ "`uname -m`" = "x86_64" ] || return 0
	local ldso="" candidate help_text flags f
	for candidate in "${VKSTORM_LD_SO:-}" /lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2; do
		if [ -n "$candidate" ] && [ -x "$candidate" ]; then
			ldso="$candidate"
			break
		fi
	done
	# glibc 2.33 and later list the supported x86-64 levels in "--help".
	if [ -n "$ldso" ]; then
		help_text="`"$ldso" --help 2>/dev/null`"
		case "$help_text" in
			*"x86-64-v3 (supported"*) return 0 ;;
			*"x86-64-v3"*) return 1 ;;
		esac
	fi
	# Older glibc: look for every x86-64-v3 feature in the first "flags" line.
	local cpuinfo="${VKSTORM_CPUINFO:-/proc/cpuinfo}"
	[ -r "$cpuinfo" ] || return 0
	flags=" `grep -m1 '^flags' "$cpuinfo" 2>/dev/null` "
	[ "$flags" = "  " ] && return 0
	for f in avx avx2 bmi1 bmi2 f16c fma movbe abm; do
		case "$flags" in
			*" $f "*) ;;
			*) return 1 ;;
		esac
	done
	return 0
}

if ! vkstorm_cpu_ok; then
	VKSTORM_CPU_MSG="Vulkanstorm needs a processor with AVX2 (x86-64-v3): Intel Haswell (2013) or newer, AMD Zen or Excavator or newer. This computer's processor does not support it."
	echo "*** $VKSTORM_CPU_MSG ***" >&2
	if [ -n "$DISPLAY$WAYLAND_DISPLAY" ] && command -v zenity >/dev/null 2>&1; then
		zenity --error --title="Vulkanstorm" --text="$VKSTORM_CPU_MSG"
	elif [ -n "$DISPLAY$WAYLAND_DISPLAY" ] && command -v kdialog >/dev/null 2>&1; then
		kdialog --title "Vulkanstorm" --error "$VKSTORM_CPU_MSG"
	fi
	exit 1
fi

# Run the program.
# Don't quote $LL_WRAPPER because, if empty, it should simply vanish from the
# command line. But DO quote "${ARGS[@]}": preserve separate args as
# individually quoted.
$LL_WRAPPER bin/do-not-directly-run-vulkanstorm-bin "${ARGS[@]}"
LL_RUN_ERR=$?

# Handle any resulting errors
if [ $LL_RUN_ERR -ne 0 ]; then
	# generic error running the binary
	echo '*** Bad shutdown ($LL_RUN_ERR). ***'
fi
