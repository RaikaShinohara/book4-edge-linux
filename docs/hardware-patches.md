# NP750XQA hardware patches

Apply these patches after the base series in the [README](../README.md).
The board DTS is modified by the patches so the DTB can be rebuilt with the
kernel. The base patch series and kernel config remain usable on their own.
The hardware series includes the CC-pin orientation fix for Samsung EmuEC and
the MSM DisplayPort AUX link-training retry. The DTS produced by the series
matches the enabled hardware nodes and wiring of the tested DTB. The tested
DTB retained disabled PMIC GLINK and SBU mux nodes that the DTS patches remove.

| Change | Observation on the test laptop |
| --- | --- |
| SDC2 microSD with GPIO71 card detect | A 116.6 GiB card enumerated as `mmcblk0` in SDR104 mode and mounted read-only. |
| S2MM006 Type-C controllers, PS8830 retimers and PTN3222 repeaters | Both ports bound to EmuEC. A USB drive behind a hub re-enumerated after hotplug on the rear port. The front port negotiated 20 V. Front-port data hotplug and the battery charge rate still need testing. |
| Native HDMI through the RTD2171 bridge | A monitor displayed an image when plugged into the laptop's HDMI port. |
| USB-C DisplayPort | An external monitor ran at 2560×1440 at 75 Hz. The second port was tested in both plug orientations and on reconnect. The first port can still need its saved mode reapplied after reconnect. See the [Plasma DP-1 workaround](../userspace/display-recovery/). |
| Dual-HDMI USB-C hub | One external output was verified on Linux. Two independent HDMI outputs from the same hub remain unverified. |

## Build

From a kernel checkout inside the fork’s directory, after applying the
base patches:

```sh
git am ../patches/hardware/*.patch
cp ../config/book4-edge.config .config
scripts/config --module TYPEC_SAMSUNG_EMUEC --disable LOCALVERSION_AUTO \
    --set-str LOCALVERSION -book4-hwtest
make olddefconfig
make -j"$(nproc)" Image vmlinuz.efi modules dtbs
```

For a microSD-only build, apply only `../patches/hardware/0001-*.patch` and
omit `--module TYPEC_SAMSUNG_EMUEC`. That build can keep using the existing
charging service.

After `sudo make modules_install`, build an initramfs for the new kernel
release (`make -s kernelrelease`). On Arch, use a copy of
[`boot/mkinitcpio-book4.conf`](../boot/mkinitcpio-book4.conf) as the configuration
and append the following when building with EmuEC:

```bash
MODULES+=(samsung_emuec ps883x)
```

Use separate names on the EFI system partition for the kernel image, initramfs
and DTB. Copy the working systemd-boot entry to point to those files and select
it from the boot menu. You can select the previous entry to recover.

For an EmuEC trial, add `systemd.mask=book4-pd-charge.service` to the test
entry's kernel options. This gives EmuEC exclusive control of the S2MM006
controllers. The service also programs the separate ISL9241 charger, so a
negotiated PD contract alone does not establish proper battery charging.
Power the laptop from a charged battery for the trial and use the previous
boot entry to restore the original charging arrangement.

## USB-C DisplayPort experiment

Apply the following after the hardware series, before building:

```sh
git am ../patches/hardware/displayport/*.patch
```

These patches cap USB-C DisplayPort signaling at HBR2, skip the MSM idle
pattern when the main link never came up, and add optional single-mixer
allocation for modes that fit the hardware limits. These three patches are
experiments separate from the tested hardware series. The HBR2 cap keeps the
existing two-lane USB-C configuration and reduces available video bandwidth.
Add `msm.bookedge_multidisplay=1` to the test entry to try the mixer change.
The rebuilt `msm` driver must be included in that entry's initramfs. Test the
adapter by itself before adding displays.

The board wiring and EmuEC driver come from
[Anatase](https://github.com/anatase-org/patchwork/tree/5fcc40d7a4bfc7ac91fc080c384b71331e23def7).
The idle-pattern guard is adapted from
[Jesse Casco's kernel patch](https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2608.1/00440.html),
tested on a Snapdragon X2 machine. The mixer experiment follows a
[multi-display report for a related NP750XQA configuration](https://discourse.ubuntu.com/t/four-independent-displays-on-ubuntu-arm64-galaxy-book4-edge-np750xqa-kb2br/91197).

## Separate DP-2 diagnostics

The overlays in [displayport/diagnostics](../patches/hardware/displayport/diagnostics/)
reproduce the DP-2 isolation and RBR/HBR tests. Compile either with
`dtc -@ -I dts -O dtb`, then apply it to a copy of the board DTB using
`fdtoverlay`. The isolation overlay disables DP-2 video. The HBR overlay
allows up to 2.7 Gbit/s per lane on DP-2. Use distinct DTB filenames and
separate boot entries for the diagnostic builds. The optional TPS3 patch in
that directory applies after the hardware series and is disabled by default.
Use `msm.bookedge_hbr2_tps3=1` to test it with HBR2 repeaters.

The active-CC-pin correction is based on the
[Book4 Edge community patch](https://github.com/n1c0la84/book4-edge-linux/blob/main/drivers/anatase/patches/0004-samsung-emuec-orientation-from-cc-pin.patch).
