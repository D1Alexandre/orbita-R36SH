#!/bin/bash
# ArkOS / EmulationStation launcher. Copy beside the orbita/ directory in ports/.
# PortMaster's sourced helpers use optional, initially unset variables.
set +u
umask 077
PORTS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$PORTS_DIR/orbita"
BIN="$GAME_DIR/bin/orbita"
GAMEDIR="$GAME_DIR"
export XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"

# Capture errors before loading PortMaster, including failures in its helpers.
LOG_DIR="${ORBITA_LOG_DIR:-$GAME_DIR/logs}"
if ! { mkdir -p "$LOG_DIR" && : > "$LOG_DIR/last-run.log"; } 2>/dev/null; then
    LOG_DIR="/dev/shm/orbita-$(id -u)-logs"
    if ! { mkdir -p "$LOG_DIR" && : > "$LOG_DIR/last-run.log"; }; then
        echo 'Nao foi possivel criar o log de abertura.' >&2
        exit 1
    fi
fi
LOG_FILE="$LOG_DIR/last-run.log"
printf 'Log do Orbita: %s\n' "$LOG_FILE"
exec > "$LOG_FILE" 2>&1
trap 'orbita_launcher_exit=$?; printf "launcher_exit=%s\n" "$orbita_launcher_exit"' EXIT
printf 'launcher_revision=2\nstarted_utc=%s\nbinary=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$BIN"

# Preserve the device's customized SDL/DRM libraries and controller mappings.
pm_loaded=0
for control_dir in "${ORBITA_PORTMASTER_DIR:-}" /opt/system/Tools/PortMaster /opt/tools/PortMaster "$XDG_DATA_HOME/PortMaster" /roms/ports/PortMaster /roms2/ports/PortMaster; do
    [ -n "$control_dir" ] || continue
    if [ -r "$control_dir/control.txt" ]; then
        export controlfolder="$control_dir"
        printf 'portmaster=%s\n' "$controlfolder"
        if source "$controlfolder/control.txt"; then
            pm_loaded=1
            if [ -n "${CFW_NAME:-}" ] && [ -r "$controlfolder/mod_${CFW_NAME}.txt" ]; then
                source "$controlfolder/mod_${CFW_NAME}.txt"
            fi
            if command -v get_controls >/dev/null 2>&1; then get_controls; fi
        else
            echo 'PortMaster: falha ao carregar control.txt; tentando SDL do sistema.'
        fi
        break
    fi
done
if [ -n "${sdl_controllerconfig:-}" ]; then
    export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi
if [ -d "$GAME_DIR/lib" ]; then
    export LD_LIBRARY_PATH="$GAME_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
DATA_DIR="${ORBITA_DATA_DIR:-$XDG_DATA_HOME/orbita}"
export ORBITA_DATA_DIR="$DATA_DIR"
printf 'data_directory=%s\nvideo_driver=%s\ncontroller_database=%s\n' \
    "$DATA_DIR" "${SDL_VIDEODRIVER:-automatic}" "${SDL_GAMECONTROLLERCONFIG_FILE:-default}"
if [ ! -x "$BIN" ]; then
    echo "Executável Linux ARM ausente: $BIN" >&2
    exit 1
fi
cd "$GAME_DIR" || exit 1
if [ "$pm_loaded" = 1 ] && [ "${1:-}" != --diagnose ] && command -v pm_platform_helper >/dev/null 2>&1; then
    echo 'stage=platform_setup'
    pm_platform_helper "$BIN"
fi
echo 'stage=application_start'
"$BIN" "$@"
orbita_application_exit=$?
printf 'application_exit=%s\n' "$orbita_application_exit"
# ArkOS/PortMaster may need its console state restored after a direct SDL port.
if [ "$pm_loaded" = 1 ] && [ "${1:-}" != --diagnose ] && command -v pm_finish >/dev/null 2>&1; then pm_finish; fi
exit "$orbita_application_exit"
