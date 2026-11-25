# Experimental DisplayPort MST on the NP750XQA

This build targets hubs that carry two independent DisplayPort streams over
one USB-C connection. It uses the [IRQ HPD v8 series][hpd], [MSM DP MST v9
series][mst] and a Book4 Edge patch for linux-next at
2637bdec0535a2044cd830eba9a79363651d9043.

The board patch restores the NP750XQA wiring and propagates USB-C DP
Attention IRQ HPD through the DRM bridge. It includes the AUX link-training
retry and carries forward the touchpad, EC, microSD, HDMI and camera wiring.
Both USB-C DisplayPort nodes expose two stream clocks and registers in the
compiled DTB. The kernel, modules, initramfs and DTB were built. Independent
video on both outputs of the same hub remains to be verified on this model.
The separate 7.2 EL2/KVM build does not contain these MST patches.

## Build

Set REPO to this checkout and start with a clean kernel tree at the commit
above. Apply the patches in this order.

~~~sh
for patch in "$REPO"/patches/mst/upstream/hpd-*.patch \
    "$REPO"/patches/mst/upstream/mst-*.patch \
    "$REPO"/patches/mst/bookedge-linux-next.patch; do
    git apply "$patch" || exit
done
cp "$REPO/config/book4-edge-mst.config" .config
make ARCH=arm64 olddefconfig
make ARCH=arm64 -j"$(nproc)" Image vmlinuz.efi modules \
    qcom/x1p42100-samsung-galaxy-book4-edge-np750xqa.dtb
~~~

Install the modules for this release and build its initramfs. Include
samsung_emuec and ps883x in addition to the usual Book4 initramfs modules.
Make sure the rebuilt msm and drm_display_helper modules and the UFS driver
are available at boot. Use separate filenames and a separate test entry for
the kernel, initramfs and DTB. With EmuEC enabled, carry over the charging
service mask from the working EmuEC entry.

On the first test, connect the hub after Plasma starts and check whether
Display Configuration lists two separate outputs from the same port.

[hpd]: https://lore.kernel.org/all/20261024-hpd-irq-events-v8-0-6ee949e7a384@oss.qualcomm.com/
[mst]: https://lore.kernel.org/all/20261119-msm-dp-mst-v9-0-ca61be4edfc3@oss.qualcomm.com/
