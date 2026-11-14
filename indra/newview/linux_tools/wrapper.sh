#!/bin/bash

# Vulkanstorm is built for x86-64-v3 (AVX2). On an older processor the viewer
# would die with an illegal-instruction fault and no explanation, so check
# first, before anything is preloaded or started.
#
# Only a positive "x86-64-v3 (supported" from ld.so ends the check early. Any
# other or unrecognised ld.so output falls through to the /proc/cpuinfo flags,
# and the viewer is refused only when those are readable and lack a flag.
#
# For tests: VKSTORM_ARCH (instead of uname -m), VKSTORM_LD_SO (the ld.so to
# ask; set but empty means none) and VKSTORM_CPUINFO (instead of
# /proc/cpuinfo); VKSTORM_CPU_CHECK_ONLY=1 exits 0 after a passed check.
vkstorm_cpu_ok() {
	[ "${VKSTORM_ARCH:-`uname -m`}" = "x86_64" ] || return 0
	local candidates candidate help_text flags f cpuinfo
	if [ -n "${VKSTORM_LD_SO+x}" ]; then
		candidates="$VKSTORM_LD_SO"
	else
		candidates="/lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
	fi
	# glibc 2.33 and later list the supported x86-64 levels in "--help".
	for candidate in $candidates; do
		if [ -x "$candidate" ]; then
			help_text="`"$candidate" --help 2>/dev/null`"
			case "$help_text" in
				*"x86-64-v3 (supported"*) return 0 ;;
			esac
			break
		fi
	done
	# Otherwise look for every x86-64-v3 feature in the first "flags" line.
	cpuinfo="${VKSTORM_CPUINFO:-/proc/cpuinfo}"
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
	VKSTORM_CPU_MSG="Vulkanstorm needs a processor with AVX2 (x86-64-v3). Most Intel Core processors from Haswell (2013) on and AMD Zen or Excavator and newer have it; some Pentium, Celeron and Atom models and some virtual machines do not. This computer's processor does not support it."
	echo "*** $VKSTORM_CPU_MSG ***" >&2
	if [ -n "$DISPLAY$WAYLAND_DISPLAY" ]; then
		if command -v zenity >/dev/null 2>&1; then
			zenity --error --title="Vulkanstorm" --text="$VKSTORM_CPU_MSG"
		elif command -v kdialog >/dev/null 2>&1; then
			kdialog --title "Vulkanstorm" --error "$VKSTORM_CPU_MSG"
		elif command -v xmessage >/dev/null 2>&1; then
			xmessage -center "$VKSTORM_CPU_MSG"
		elif command -v notify-send >/dev/null 2>&1; then
			notify-send -u critical "Vulkanstorm" "$VKSTORM_CPU_MSG"
		fi
	fi
	exit 1
fi
[ -n "$VKSTORM_CPU_CHECK_ONLY" ] && exit 0

## Here are some configuration options for Linux Client Users.

## - Avoids using any OpenAL audio driver.
#export LL_BAD_OPENAL_DRIVER=x

## GL Driver Options
export mesa_glthread=true

# Cache NVIDIA display-hardware detection without requiring diagnostic tools.
# Require the NVIDIA driver too; Nouveau uses Mesa and does not need this policy.
has_nvidia_gpu=false
for pci_device in /sys/bus/pci/devices/*; do
    [[ -r "$pci_device/vendor" && -r "$pci_device/class" ]] || continue
    if [[ "$(< "$pci_device/vendor")" == "0x10de" &&
          "$(< "$pci_device/class")" == 0x03* &&
          "$(readlink -- "$pci_device/driver" 2>/dev/null)" == */nvidia ]]; then
        has_nvidia_gpu=true
        break
    fi
done

# Leave SDL backend selection and NVIDIA-specific variables alone on other GPUs.
if "$has_nvidia_gpu"; then
    # Detect the desktop session and read SDL's preferred backend once.
    is_wayland=false
    if [[ -n "${WAYLAND_DISPLAY:-}" || "${XDG_SESSION_TYPE:-}" == "wayland" ]]; then
        is_wayland=true
    fi
    sdl_video_driver="${SDL_VIDEO_DRIVER:-${SDL_VIDEODRIVER:-}}"

    # On Wayland, default to X11/XWayland without replacing an explicit backend.
    if "$is_wayland" && [[ -z "$sdl_video_driver" ]]; then
        export SDL_VIDEO_DRIVER=x11
        export SDL_VIDEODRIVER=x11
        sdl_video_driver=x11
    fi

    # Respect explicit backends; otherwise assume X11 outside Wayland sessions.
    is_using_x11_sdl=false
    if [[ -n "$sdl_video_driver" ]]; then
        if [[ "$sdl_video_driver" == "x11" ]]; then
            is_using_x11_sdl=true
        fi
    elif ! "$is_wayland"; then
        is_using_x11_sdl=true
    fi

    # Native Wayland and ambiguous fallback lists keep NVIDIA threading disabled.
    if "$is_using_x11_sdl"; then
        export __GL_THREADED_OPTIMIZATIONS=1
    else
        export __GL_THREADED_OPTIMIZATIONS=0
    fi
fi

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

# <Vulkanstorm> The bundled allocator is found next to this script (RUN_PATH,
# resolved through symlinks above), never in the caller's working directory,
# and LD_PRELOAD gets no leading ":" when it was empty.
FSJEMALLOC="${RUN_PATH}/lib/libjemalloc.so"
if [ -f "${FSJEMALLOC}" ]
then
        echo "Using jemalloc"
        export LD_PRELOAD="${LD_PRELOAD:+${LD_PRELOAD}:}${FSJEMALLOC}"
fi

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
