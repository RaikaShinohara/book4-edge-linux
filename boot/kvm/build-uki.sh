#!/usr/bin/env bash
set -euo pipefail

if (( $# != 4 )); then
    echo "Usage: $0 ESP WORKING_ENTRY EL2_DTB OUTPUT_EFI" >&2
    exit 2
fi
esp=$1
entry=$2
dtb=$3
output=$4
[[ ! -e $output ]]
fdtget -ts "$dtb" / compatible | grep -qw 'samsung,galaxy-book4-edge-np750xqa'
[[ $(fdtget -ts "$dtb" /soc@0/gpu@3d00000/zap-shader status) == disabled ]]

kernel=$(awk '$1=="linux" {print $2; exit}' "$entry")
options=$(awk '$1=="options" {sub(/^[[:space:]]*options[[:space:]]+/, ""); print; exit}' "$entry")
mapfile -t initrds < <(awk '$1=="initrd" {print $2}' "$entry")
[[ $kernel == /* && -s $esp$kernel && " $options " == *" root="* && ${#initrds[@]} -gt 0 ]]
args=()
for initrd in "${initrds[@]}"; do
    [[ $initrd == /* && -s $esp$initrd ]]
    args+=("--initrd=$esp$initrd")
done
options=$(sed -E 's/(^|[[:space:]])(systemd\.setenv=BOOKEDGE_TEST|drm\.debug|efi|loglevel|systemd\.unit)=[^[:space:]]+//g' <<<"$options")
ukify --config=/dev/null build --stub=/usr/lib/systemd/boot/efi/linuxaa64.efi.stub \
    --linux="$esp$kernel" "${args[@]}" --devicetree="$dtb" \
    --cmdline="$options efi=noruntime,novamap id_aa64mmfr0.ecv=1 loglevel=5 systemd.setenv=BOOKEDGE_TEST=kvm_el2_uki" \
    --output="$output"
