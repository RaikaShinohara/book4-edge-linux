# DisplayPort MST, native HDMI and KVM on the NP750XQA

This build targets hubs that carry two independent DisplayPort streams over
one USB-C connection. It uses the [IRQ HPD v8 series][hpd], [MSM DP MST v9
series][mst] and a Book4 Edge patch for linux-next at
2637bdec0535a2044cd830eba9a79363651d9043.

The board patch restores the NP750XQA wiring and propagates USB-C DP
Attention IRQ HPD through the DRM bridge. It includes the AUX link-training
retry and carries forward the touchpad, EC, microSD, HDMI and camera wiring.
Both USB-C DisplayPort nodes expose two stream clocks and registers. The
Samsung EmuEC bridge supplies the atomic state callbacks required by DRM in
this kernel. Qualcomm DP initializes the single-stream HDMI bridge and reads
the monitor's modes during hotplug. The DPU can use one mixer per display
when the mode fits, leaving mixer resources for the other outputs.

On the tested NP750XQA, Plasma extended the desktop across the laptop
panel, two 1920×1080 at 60 Hz outputs from the same USB-C hub, and the
native HDMI output at 2560×1440 at 75 Hz. The same boot entered EL2 and
passed the KVM API and VM-creation checks. Long idle sessions and
suspend/resume with this kernel are still under evaluation.

## Build

Set REPO to this checkout and start with a clean kernel tree at the commit
above. Apply the patches in this order.

~~~sh
for patch in "$REPO"/patches/mst/upstream/hpd-*.patch \
    "$REPO"/patches/mst/upstream/mst-*.patch \
    "$REPO"/patches/mst/bookedge-linux-next.patch \
    "$REPO"/patches/hardware/displayport/0003-*.patch \
    "$REPO"/patches/mst/fixes/*.patch; do
    git apply "$patch" || exit
done
cp "$REPO/config/book4-edge-mst.config" .config
make ARCH=arm64 olddefconfig
make ARCH=arm64 DTC_FLAGS=-@ -j"$(nproc)" vmlinuz.efi modules \
    qcom/x1p42100-samsung-galaxy-book4-edge-np750xqa.dtb
release=$(make -s ARCH=arm64 kernelrelease)
sudo make ARCH=arm64 INSTALL_MOD_STRIP=1 modules_install
~~~

Use the [EL2 setup](kvm.md) to check firmware support before building the
combined boot entry. The symbol-enabled board DTB lets `fdtoverlay` apply
the X1 EL2 overlay to the same tree that describes both USB-C outputs.

~~~sh
stage=$(mktemp -d)
board=arch/arm64/boot/dts/qcom/x1p42100-samsung-galaxy-book4-edge-np750xqa.dtb
dtc -@ -I dts -O dtb -o "$stage/x1-el2.dtbo" "$REPO/dts/x1-el2.dtso"
fdtoverlay -i "$board" -o "$stage/bookedge-el2-mst-hdmi.dtb" "$stage/x1-el2.dtbo"
sed -e 's/^MODULES=(/MODULES=(virtio_pci virtio_blk /' -e 's/ modconf kms / modconf /' \
    "$REPO/boot/mkinitcpio-book4.conf" > "$stage/mkinitcpio.conf"
printf '%s\n' 'MODULES+=(simpledrm samsung_emuec ps883x)' >> "$stage/mkinitcpio.conf"
sudo mkinitcpio --nopost -k "$release" -c "$stage/mkinitcpio.conf" \
    -g "$stage/initramfs.img"
~~~

Set `WORKING_ENTRY` to the path of the working Linux entry with an `options`
line, and use `ukify` to embed the new DTB and initramfs. This copies your
root filesystem settings and enables single-mixer allocation for this entry.

~~~sh
options=$(sudo awk '$1=="options" {sub(/^[[:space:]]*options[[:space:]]+/, ""); print; exit}' "$WORKING_ENTRY")
[[ " $options " == *" root="* ]] || exit 1
options=$(sed -E 's/(^|[[:space:]])(systemd\.setenv=BOOKEDGE_TEST|drm\.debug|efi|id_aa64mmfr0\.ecv|loglevel|systemd\.unit|msm\.bookedge_multidisplay)=[^[:space:]]+//g' <<< "$options")
sudo ukify --config=/dev/null build --stub=/usr/lib/systemd/boot/efi/linuxaa64.efi.stub \
    --linux=arch/arm64/boot/vmlinuz.efi --initrd="$stage/initramfs.img" \
    --devicetree="$stage/bookedge-el2-mst-hdmi.dtb" --uname="$release" \
    --cmdline="$options efi=noruntime,novamap id_aa64mmfr0.ecv=1 loglevel=5 systemd.mask=book4-pd-charge.service msm.bookedge_multidisplay=1 systemd.setenv=BOOKEDGE_TEST=kvm_mst_v3" \
    --output="$stage/bookedge-el2-mst-hdmi.efi"
~~~

Use the same ARM64 UEFI Shell, `slbounce.efi` and ESP `tcblaunch.exe` as for
the working EL2 boot. Put the new UKI, shell and slbounce in
`EFI/bookedge-el2-mst-hdmi/`. Prepare `startup.nsh` with CRLF line endings.

~~~sh
awk '{sub(/\r$/, ""); printf "%s\r\n", $0}' "$REPO/boot/kvm/startup-mst-hdmi.nsh" > "$stage/startup.nsh"
~~~

Install that script alongside the UKI, then install
[bookedge-el2-mst-hdmi.conf] in `loader/entries/`. Keep the working entry in
the boot menu while testing.

Start with both HDMI outputs of one hub and native HDMI connected. In
Plasma, `kscreen-doctor -o` should show the two DisplayPort outputs and
`HDMI-A-1` with separate positions. Run `python boot/kvm/check-kvm.py` as
the desktop user to check KVM in that boot.

[hpd]: https://lore.kernel.org/all/20261024-hpd-irq-events-v8-0-6ee949e7a384@oss.qualcomm.com/
[mst]: https://lore.kernel.org/all/20261119-msm-dp-mst-v9-0-ca61be4edfc3@oss.qualcomm.com/
[bookedge-el2-mst-hdmi.conf]: ../boot/kvm/bookedge-el2-mst-hdmi.conf
