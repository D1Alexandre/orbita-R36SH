#!/bin/bash
# Focused, read-only follow-up after r36-probe.sh. No display or service changes.
set -u
export LC_ALL=C

echo '=== frontend processes ==='
ps ax -o pid,comm | grep -E 'emulation|retroarch|SDL|gptokey|oga_' || true
echo '=== PortMaster controls ==='
for path in /opt/system/Tools/PortMaster/control.txt /opt/system/Tools/PortMaster/device_info.txt; do
    if [ -f "$path" ]; then
        echo "$path"
        stat -c 'size=%s owner=%U mode=%a' "$path"
        if [ "$(basename "$path")" = control.txt ]; then
            sed -n '1,170p' "$path"
        fi
    fi
done
echo '=== SDL headers ==='
for path in /usr/include/SDL2/SDL.h /usr/local/include/SDL2/SDL.h; do
    [ ! -f "$path" ] || ls -l "$path"
done
echo '=== media libraries ==='
find /usr/lib /usr/local/lib -maxdepth 3 \( -iname '*mpp*' -o -iname '*vpu*' \) -print 2>/dev/null || true
echo '=== storage errors ==='
dmesg 2>/dev/null | grep -Ei 'I/O error|Buffer I/O|FAT-fs|EXT4-fs error|mmc.*error' | tail -n 25 || true
echo '=== frontend config paths ==='
for path in /etc/emulationstation/es_systems.cfg /home/ark/.emulationstation/es_systems.cfg; do
    if [ -r "$path" ]; then
        grep -A 10 -B 2 '<name>ports</name>' "$path" || true
    fi
done
echo '=== PortMaster launcher examples ==='
find /roms/ports -maxdepth 1 -type f -iname '*.sh' -printf '%f\n' 2>/dev/null | head -n 15 || true
