# EL2 and KVM on the Galaxy Book4 Edge NP750XQA

On the tested NP750XQA, the usual Linux boot enters EL1 even with
CONFIG_KVM=y. Linux booted into Plasma at EL2 using the same kernel and the
unified image below. KVM initialized in VHE mode. A Debian 13 ARM64 guest booted
with Firecracker and ran an Xfce session over a local TigerVNC connection.

## Build the EL2 image

Use a board DTB built with symbols enabled (DTC_FLAGS=-@ on the kernel build).
The [overlay](../dts/x1-el2.dtso) is from the Linux Qualcomm X1 device trees.
Follow the [slbounce firmware probe](https://github.com/TravMurav/slbounce)
before attempting a Linux boot. The firmware and a compatible signed
tcblaunch.exe from your Windows installation are needed.

~~~sh
dtc -@ -I dts -O dtb -o x1-el2.dtbo dts/x1-el2.dtso
fdtoverlay -i working-bookedge.dtb -o bookedge-el2.dtb x1-el2.dtbo
sudo bash boot/kvm/build-uki.sh /efi /efi/loader/entries/YOUR_WORKING_ENTRY.conf \
    bookedge-el2.dtb bookedge-kvm-uki.efi
~~~

Use an ARM64 [UEFI Shell](https://github.com/pbatard/UEFI-Shell/releases) and
[slbounce v5](https://github.com/TravMurav/slbounce/releases/tag/v5). Place
shellaa64.efi, slbounce.efi, bookedge-kvm-uki.efi and the supplied startup.nsh
in /efi/EFI/bookedge-kvm-uki/. The slbounce driver reads the Windows
tcblaunch.exe from the root of that ESP. Install the example entry in
/efi/loader/entries under its own filename and select it manually at restart.

systemd-stub places the embedded EL2 DTB in the EFI configuration table
where slbounce finds it during ExitBootServices. The earlier attempt that
supplied the DTB only through the kernel's dtb= option stalled. The image
bundles the existing initrds in their original order. slbounce loads from
the dedicated UEFI Shell entry, so the usual loader does not load it.
Keep the existing boot entry available while testing.

Check for EL2 with journalctl -k -b and run boot/kvm/check-kvm.py as your
normal user. USB-C MST still uses a separate experimental kernel build.
