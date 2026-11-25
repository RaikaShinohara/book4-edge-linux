#!/usr/bin/env bash
set -euo pipefail

if (( EUID != 0 )); then
    printf '%s\n' 'Ejecuta el instalador con sudo desde Konsole.' >&2
    exit 1
fi

if ! grep -azFxq 'samsung,galaxy-book4-edge-np750xqa' /sys/firmware/devicetree/base/compatible; then
    printf '%s\n' 'Estos perfiles son para el Galaxy Book4 Edge NP750XQA.' >&2
    exit 1
fi

cpu=/sys/devices/system/cpu/cpufreq
gpu=/sys/class/devfreq/3d00000.gpu
if [[ $(<"$cpu/policy0/cpuinfo_max_freq") != 3244800 ||
      $(<"$cpu/policy4/cpuinfo_max_freq") != 3417600 ]] ||
   ! grep -qw 1107000000 "$gpu/available_frequencies"; then
    printf '%s\n' 'Las frecuencias que publica el kernel no coinciden con este equipo.' >&2
    exit 1
fi

for unit in power-profiles-daemon.service tlp.service cpupower.service auto-cpufreq.service; do
    if systemctl is-active --quiet "$unit"; then
        printf 'Ya hay otro gestor activo: %s\n' "$unit" >&2
        exit 1
    fi
done

backup=/var/lib/bookedge-energy
settings=/etc/tuned/ppd.conf
if systemctl is-active --quiet tuned.service && [[ ! -f "$backup/ppd.conf" ]]; then
    printf '%s\n' 'TuneD ya está activo con una configuración distinta.' >&2
    exit 1
fi

if ! command -v tuned-ppd >/dev/null; then
    printf '%s\n' 'Install tuned-ppd before running this script.' >&2
    exit 1
fi

# The port's optional legacy rule pins Adreno to 1.107 GHz.
gpu_floor=/etc/udev/rules.d/90-book4-gpu-floor.rules
if [[ -f $gpu_floor ]]; then
    original_rule=$(dirname -- "${BASH_SOURCE[0]}")/../udev/90-book4-gpu-floor.rules
    if ! cmp -s -- "$gpu_floor" "$original_rule" || [[ -e $gpu_floor.disabled ]]; then
        printf 'Check the local GPU floor rule before continuing: %s\n' "$gpu_floor" >&2
        exit 1
    fi
    mv -- "$gpu_floor" "$gpu_floor.disabled"
    udevadm control --reload
fi

install -d -m 0700 "$backup"
for file in ppd.conf active_profile profile_mode; do
    if [[ -f /etc/tuned/$file && ! -e "$backup/$file" ]]; then
        cp -p -- "/etc/tuned/$file" "$backup/$file"
    fi
done

write_profile() {
    local name=$1 summary=$2 cpu0_max=$3 cpu4_max=$4 gpu_max=$5
    local directory=/etc/tuned/profiles/bookedge-$name
    install -d -m 0755 "$directory"
    cat > "$directory/tuned.conf" <<PROFILE
[main]
summary=$summary

[sysfs]
$cpu/boost=1
$cpu/policy0/scaling_governor=schedutil
$cpu/policy4/scaling_governor=schedutil
$cpu/policy0/scaling_max_freq=$cpu0_max
$cpu/policy4/scaling_max_freq=$cpu4_max
$gpu/governor=simple_ondemand
$gpu/min_freq=280000000
$gpu/max_freq=$gpu_max
PROFILE
    chmod 0644 "$directory/tuned.conf"
}

write_profile ahorro 'Galaxy Book4 Edge - Ahorro' 1920000 1920000 550000000
write_profile equilibrado 'Galaxy Book4 Edge - Equilibrado' 2976000 2976000 940000000
write_profile rendimiento 'Galaxy Book4 Edge - Rendimiento' 3244800 3417600 1107000000

cat > "$settings" <<'PPD'
[main]
default=balanced
battery_detection=true
sysfs_acpi_monitor=false

[profiles]
power-saver=bookedge-ahorro
balanced=bookedge-equilibrado
performance=bookedge-rendimiento

[battery]
power-saver=bookedge-ahorro
balanced=bookedge-equilibrado
performance=bookedge-rendimiento
PPD

if ! systemctl is-active --quiet tuned.service; then
    printf '%s\n' bookedge-equilibrado > /etc/tuned/active_profile
    printf '%s\n' manual > /etc/tuned/profile_mode
fi

if systemctl is-active --quiet tuned-ppd.service; then
    systemctl restart tuned-ppd.service
    systemctl enable tuned-ppd.service
else
    systemctl enable --now tuned-ppd.service
fi
service=org.freedesktop.UPower.PowerProfiles
profile_path=/org/freedesktop/UPower/PowerProfiles
set_profile() {
    busctl --system set-property "$service" "$profile_path" "$service" ActiveProfile s "$1"
}

verify_profile() {
    local profile=$1 cpu0_max=$2 cpu4_max=$3 gpu_max=$4
    set_profile "$profile" || return 1
    for attempt in {1..30}; do
        if [[ $(<"$cpu/policy0/scaling_max_freq") == "$cpu0_max" &&
              $(<"$cpu/policy4/scaling_max_freq") == "$cpu4_max" &&
              $(<"$gpu/max_freq") == "$gpu_max" &&
              $(<"$gpu/min_freq") == 280000000 &&
              $(busctl --system get-property "$service" "$profile_path" "$service" ActiveProfile) == "s \"$profile\"" ]]; then
            printf '%s verificado (CPU %s/%s kHz, GPU %s Hz)\n' "$profile" "$cpu0_max" "$cpu4_max" "$gpu_max"
            return 0
        fi
        sleep 0.2
    done
    printf 'El kernel no confirmó los límites de %s. CPU %s/%s, GPU %s–%s Hz.\n' "$profile" \
        "$(<"$cpu/policy0/scaling_max_freq")" "$(<"$cpu/policy4/scaling_max_freq")" \
        "$(<"$gpu/min_freq")" "$(<"$gpu/max_freq")" >&2
    return 1
}

verification=0
verify_profile power-saver 1920000 1920000 550000000 || verification=1
verify_profile performance 3244800 3417600 1107000000 || verification=1
verify_profile balanced 2976000 2976000 940000000 || verification=1
busctl --system get-property "$service" "$profile_path" "$service" ActiveProfile
exit "$verification"
