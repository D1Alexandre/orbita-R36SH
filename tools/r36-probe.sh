#!/usr/bin/env bash
# Orbita R36S/H hardware probe.
# Read-only, no root required. The generated report redacts network addresses
# and common credential formats so it can be shared during port bring-up.

set -u
set -o pipefail

export LC_ALL=C
export LANG=C

PROBE_VERSION="1"
INCLUDE_DMESG=0
OUTPUT_FILE=""

usage() {
    cat <<'EOF'
Usage: r36-probe.sh [--output FILE] [--include-dmesg]

Collects the hardware and Linux capabilities needed by the Orbita R36S/H port.
The probe is read-only and does not require root.

Options:
  --output FILE       Write the report to FILE instead of the current directory.
  --include-dmesg     Include filtered display/media/input/audio kernel messages.
  -h, --help          Show this help.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            [ "$#" -ge 2 ] || { echo "--output requires a file" >&2; exit 2; }
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --include-dmesg)
            INCLUDE_DMESG=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

timestamp="$(date -u '+%Y%m%dT%H%M%SZ' 2>/dev/null || date '+%Y%m%d-%H%M%S')"
if [ -z "$OUTPUT_FILE" ]; then
    output_dir="${ORB_PROBE_OUTPUT_DIR:-$PWD}"
    if [ ! -d "$output_dir" ] || [ ! -w "$output_dir" ]; then
        output_dir="${TMPDIR:-/tmp}"
    fi
    OUTPUT_FILE="$output_dir/r36-probe-$timestamp.txt"
fi

case "$OUTPUT_FILE" in
    /*) ;;
    *) OUTPUT_FILE="$PWD/$OUTPUT_FILE" ;;
esac

umask 077
mkdir -p "$(dirname "$OUTPUT_FILE")" || {
    echo "Cannot create output directory for: $OUTPUT_FILE" >&2
    exit 1
}

redact_stream() {
    sed -E \
        -e 's/([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}/[MAC]/g' \
        -e 's/([0-9]{1,3}\.){3}[0-9]{1,3}/[IPv4]/g' \
        -e 's/(serial|Serial|SERIAL)([[:space:]=:]+)[[:alnum:]_.:-]+/\1\2[REDACTED]/g' \
        -e 's/xck_[A-Za-z0-9_-]+/[API_KEY]/g' \
        -e 's/(Authorization:|Bearer)[[:space:]]+[A-Za-z0-9._~+\/-]+/\1 [REDACTED]/g'
}

if command -v tee >/dev/null 2>&1; then
    exec > >(redact_stream | tee "$OUTPUT_FILE") 2>&1
else
    echo "warning: tee not found; report will only be written to $OUTPUT_FILE" >&2
    exec > "$OUTPUT_FILE" 2>&1
fi

section() {
    printf '\n================================================================\n'
    printf '%s\n' "$1"
    printf '================================================================\n'
}

capture() {
    label="$1"
    shift
    printf '\n--- %s ---\n' "$label"
    printf '$'
    printf ' %q' "$@"
    printf '\n'
    "$@"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        printf '[command unavailable or failed: exit %d]\n' "$rc"
    fi
    return 0
}

capture_shell() {
    label="$1"
    script="$2"
    printf '\n--- %s ---\n' "$label"
    printf '$ %s\n' "$script"
    bash -o pipefail -c "$script"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        printf '[command unavailable or failed: exit %d]\n' "$rc"
    fi
    return 0
}

capture_if_present() {
    label="$1"
    shift
    if command -v "$1" >/dev/null 2>&1; then
        capture "$label" "$@"
    else
        printf '\n--- %s ---\n[missing command: %s]\n' "$label" "$1"
    fi
}

capture_timeout() {
    label="$1"
    seconds="$2"
    shift 2
    if ! command -v "$1" >/dev/null 2>&1; then
        printf '\n--- %s ---\n[missing command: %s]\n' "$label" "$1"
        return 0
    fi
    if command -v timeout >/dev/null 2>&1; then
        capture "$label" timeout "${seconds}s" "$@"
    else
        capture "$label" "$@"
    fi
}

read_dt_file() {
    path="$1"
    if [ -r "$path" ]; then
        tr '\000' '\n' < "$path" | sed '/^$/d'
    else
        echo "[not readable: $path]"
    fi
}

count_nodes() {
    directory="$1"
    pattern="$2"
    find "$directory" -mindepth 1 -maxdepth 1 -name "$pattern" -print 2>/dev/null | wc -l | tr -d ' '
}

printf 'Orbita R36S/H hardware probe\n'
printf 'probe_version=%s\n' "$PROBE_VERSION"
printf 'created_utc=%s\n' "$timestamp"
printf 'report=%s\n' "$OUTPUT_FILE"
printf 'read_only=yes\n'
printf 'addresses_redacted=yes\n'
printf 'include_dmesg=%s\n' "$INCLUDE_DMESG"

section "1. SYSTEM IDENTITY"
capture "Kernel name" uname -s
capture "Kernel release" uname -r
capture "Kernel build" uname -v
capture "Machine architecture" uname -m
capture_shell "Numeric user and groups" 'printf "uid="; id -u; printf "gid="; id -g; printf "groups="; id -G'
capture_if_present "Userspace word size" getconf LONG_BIT
capture_if_present "glibc reported by getconf" getconf GNU_LIBC_VERSION
capture_shell "C library loader version" 'ldd --version 2>&1 | head -n 4'
capture_shell "OS release" 'cat /etc/os-release 2>/dev/null || true'

printf '\n--- Device tree model ---\n'
read_dt_file /proc/device-tree/model
printf '\n--- Device tree compatible strings ---\n'
read_dt_file /proc/device-tree/compatible

capture_if_present "CPU topology" lscpu
capture_shell "CPU information (identifiers intentionally omitted)" \
    "grep -E '^(processor|model name|Features|Hardware|Revision|CPU architecture|BogoMIPS)' /proc/cpuinfo 2>/dev/null || true"
capture_shell "Memory and swap" \
    "grep -E '^(MemTotal|MemFree|MemAvailable|Buffers|Cached|SwapTotal|SwapFree|CmaTotal|CmaFree):' /proc/meminfo 2>/dev/null || true"
capture_shell "CPU frequency policy" '
for p in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$p" ] || continue
    echo "[$p]"
    for f in scaling_driver scaling_governor scaling_cur_freq scaling_min_freq scaling_max_freq cpuinfo_max_freq; do
        [ -r "$p/$f" ] && printf "%s=" "$f" && cat "$p/$f"
    done
done'

section "2. STORAGE AND FILESYSTEM"
capture_if_present "Mounted filesystems" df -hT
capture_if_present "Block devices" lsblk -o NAME,TYPE,SIZE,FSTYPE,ROTA,MODEL,MOUNTPOINT
capture_shell "Relevant application locations" '
for p in /roms /roms2 /opt/system/Tools/PortMaster /opt/tools/PortMaster /roms/ports/PortMaster /roms2/ports/PortMaster; do
    if [ -e "$p" ]; then ls -ld "$p"; else echo "missing $p"; fi
done'

section "3. DISPLAY, DRM, EGL AND GPU"
capture_shell "DRM device nodes" 'ls -la /dev/dri 2>/dev/null || true'
capture_shell "DRM cards and kernel drivers" '
for card in /sys/class/drm/card[0-9]*; do
    [ -e "$card" ] || continue
    echo "[$card]"
    readlink -f "$card/device/driver" 2>/dev/null || true
    [ -r "$card/device/modalias" ] && cat "$card/device/modalias"
done'
capture_shell "DRM connectors and modes" '
for conn in /sys/class/drm/card*-*; do
    [ -d "$conn" ] || continue
    echo "[$conn]"
    for f in status enabled modes; do
        [ -r "$conn/$f" ] && printf "%s:\n" "$f" && cat "$conn/$f"
    done
done'
capture_timeout "EGL information" 8 eglinfo -B
capture_timeout "OpenGL ES information" 8 es2_info
capture_timeout "Desktop OpenGL information" 8 glxinfo -B
capture_if_present "SDL2 version" sdl2-config --version

section "4. H.264 HARDWARE DECODE"
capture_shell "Video and Rockchip media nodes" '
for p in /dev/video* /dev/media* /dev/mpp_service /dev/vpu_service /dev/rkvdec /dev/hevc_service /dev/iep /dev/rga; do
    [ -e "$p" ] && ls -l "$p"
done
true'
capture_shell "Loaded media/GPU modules" \
    "lsmod 2>/dev/null | grep -Ei 'rockchip|hantro|rkvdec|vpu|mpp|v4l2|videobuf|mali|panfrost|drm|rga' || true"
capture_if_present "V4L2 devices" v4l2-ctl --list-devices

for video_dev in /dev/video*; do
    [ -e "$video_dev" ] || continue
    capture_timeout "V4L2 capabilities: $video_dev" 5 v4l2-ctl -d "$video_dev" -D
    capture_timeout "V4L2 output formats: $video_dev" 5 v4l2-ctl -d "$video_dev" --list-formats-out
    capture_timeout "V4L2 capture formats: $video_dev" 5 v4l2-ctl -d "$video_dev" --list-formats-ext
done

capture_if_present "FFmpeg version" ffmpeg -version
capture_if_present "FFmpeg hardware accelerators" ffmpeg -hide_banner -hwaccels
capture_shell "FFmpeg H.264 decoders" \
    "ffmpeg -hide_banner -decoders 2>/dev/null | grep -Ei '(^|[[:space:]])h264|rkmpp|v4l2' || true"
capture_shell "Media-related shared libraries" \
    "ldconfig -p 2>/dev/null | grep -Ei 'lib(avcodec|avutil|mpp|rockchip|v4l2|drm|gbm|EGL|GLES|mali|SDL2)' || true"
capture_shell "Relevant kernel configuration" '
pattern="CONFIG_(VIDEO|MEDIA|V4L2|DRM|ROCKCHIP|HANTRO|RKVDEC|MALI|PANFROST|DMA_SHARED_BUFFER)"
if [ -r /proc/config.gz ] && command -v zgrep >/dev/null 2>&1; then
    zgrep -E "$pattern" /proc/config.gz || true
elif [ -r "/boot/config-$(uname -r)" ]; then
    grep -E "$pattern" "/boot/config-$(uname -r)" || true
else
    echo "kernel configuration unavailable"
fi'

section "5. AUDIO"
capture_shell "ALSA cards" 'cat /proc/asound/cards 2>/dev/null || true'
capture_shell "ALSA PCM devices" 'cat /proc/asound/pcm 2>/dev/null || true'
capture_if_present "ALSA playback hardware" aplay -l
capture_if_present "ALSA device names" aplay -L
capture_if_present "ALSA mixer controls" amixer scontrols

section "6. INPUT AND SDL CONTROLLERS"
capture_shell "Kernel input devices" 'cat /proc/bus/input/devices 2>/dev/null || true'
capture_shell "Input event names" '
for ev in /sys/class/input/event*; do
    [ -e "$ev" ] || continue
    printf "%s: " "$(basename "$ev")"
    cat "$ev/device/name" 2>/dev/null || echo unknown
done'
capture_shell "Persistent input paths" '
ls -la /dev/input/by-path 2>/dev/null || true
ls -la /dev/input/by-id 2>/dev/null || true'
capture_shell "SDL and frontend environment" '
for key in SDL_VIDEODRIVER SDL_AUDIODRIVER SDL_GAMECONTROLLERCONFIG_FILE XDG_SESSION_TYPE DISPLAY WAYLAND_DISPLAY; do
    eval "value=\${$key-}"
    printf "%s=%s\n" "$key" "$value"
done'

section "7. NETWORK CAPABILITY (NO CONNECTIVITY TEST)"
capture_shell "Network interfaces without addresses" '
for net in /sys/class/net/*; do
    [ -e "$net" ] || continue
    name=$(basename "$net")
    state=$(cat "$net/operstate" 2>/dev/null || echo unknown)
    carrier=$(cat "$net/carrier" 2>/dev/null || echo unknown)
    speed=$(cat "$net/speed" 2>/dev/null || echo unknown)
    driver=$(basename "$(readlink -f "$net/device/driver" 2>/dev/null)" 2>/dev/null || echo unknown)
    printf "%s state=%s carrier=%s speed_mbps=%s driver=%s\n" "$name" "$state" "$carrier" "$speed" "$driver"
done'
capture_timeout "NetworkManager device states" 8 nmcli -t -f DEVICE,TYPE,STATE device status
capture_if_present "Wireless devices" iw dev
capture_if_present "Radio kill switches" rfkill list
capture_shell "Loaded network modules" \
    "lsmod 2>/dev/null | grep -Ei 'wifi|wlan|80211|cfg80211|rtl|ath|mt76|brcm|usbnet|r8152|cdc_ether' || true"

section "8. THERMAL AND POWER"
capture_shell "Thermal zones" '
for zone in /sys/class/thermal/thermal_zone*; do
    [ -d "$zone" ] || continue
    type=$(cat "$zone/type" 2>/dev/null || echo unknown)
    temp=$(cat "$zone/temp" 2>/dev/null || echo unknown)
    printf "%s type=%s temp_millic=%s\n" "$(basename "$zone")" "$type" "$temp"
done'
capture_shell "Power supplies" '
for supply in /sys/class/power_supply/*; do
    [ -d "$supply" ] || continue
    echo "[$supply]"
    for f in type status capacity voltage_now current_now; do
        [ -r "$supply/$f" ] && printf "%s=" "$f" && cat "$supply/$f"
    done
done'

section "9. BUILD AND RUNTIME TOOLS"
for tool in bash gcc cc clang cmake ninja make pkg-config python3 git; do
    if command -v "$tool" >/dev/null 2>&1; then
        capture_shell "$tool version" "$tool --version 2>&1 | head -n 3"
    else
        printf '\n--- %s version ---\n[missing command: %s]\n' "$tool" "$tool"
    fi
done
capture_shell "pkg-config libraries" '
if command -v pkg-config >/dev/null 2>&1; then
    for lib in sdl2 libavcodec libavutil opus alsa libdrm gbm egl glesv2 libcurl mbedtls; do
        if pkg-config --exists "$lib"; then
            printf "%s=%s\n" "$lib" "$(pkg-config --modversion "$lib" 2>/dev/null)"
        else
            printf "%s=missing\n" "$lib"
        fi
    done
else
    echo "pkg-config unavailable"
fi'

if [ "$INCLUDE_DMESG" -eq 1 ]; then
    section "10. FILTERED KERNEL LOG"
    capture_shell "Relevant dmesg lines" \
        "dmesg 2>/dev/null | grep -Ei 'rockchip|rk3326|drm|dsi|panel|vpu|hantro|rkvdec|mpp|v4l2|mali|panfrost|alsa|audio|input|joy|gamepad|usb|wlan|wifi' | tail -n 400 || true"
else
    section "10. FILTERED KERNEL LOG"
    echo "Skipped. Re-run with --include-dmesg if media/display detection is inconclusive."
fi

section "11. AUTOMATIC SUMMARY"
printf 'ARCH=%s\n' "$(uname -m 2>/dev/null || echo unknown)"
printf 'KERNEL=%s\n' "$(uname -r 2>/dev/null || echo unknown)"
printf 'MODEL='
read_dt_file /proc/device-tree/model | head -n 1
printf 'DRM_CARDS=%s\n' "$(count_nodes /dev/dri 'card*')"
printf 'DRM_RENDER_NODES=%s\n' "$(count_nodes /dev/dri 'renderD*')"
printf 'VIDEO_NODES=%s\n' "$(count_nodes /dev 'video*')"
printf 'MEDIA_NODES=%s\n' "$(count_nodes /dev 'media*')"
printf 'INPUT_EVENTS=%s\n' "$(count_nodes /dev/input 'event*')"
printf 'NETWORK_INTERFACES=%s\n' "$(count_nodes /sys/class/net '*')"
printf 'MPP_SERVICE=%s\n' "$([ -e /dev/mpp_service ] && echo yes || echo no)"
printf 'RKVDEC_NODE=%s\n' "$([ -e /dev/rkvdec ] && echo yes || echo no)"
printf 'VPU_SERVICE=%s\n' "$([ -e /dev/vpu_service ] && echo yes || echo no)"
printf 'FFMPEG=%s\n' "$(command -v ffmpeg 2>/dev/null || echo missing)"
printf 'SDL2=%s\n' "$(command -v sdl2-config 2>/dev/null || echo missing)"

section "PROBE COMPLETE"
printf 'Report written to: %s\n' "$OUTPUT_FILE"
printf 'No system settings were changed. Review the report before publishing it publicly.\n'

