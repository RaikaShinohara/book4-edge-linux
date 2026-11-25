# Samsung Galaxy Book4 Edge EC driver

The out-of-tree driver exports battery, AC and temperature readings. On the
NP750XQA it also reports Fn+F11 as `KEY_BATTERY`. The event is read from the
EC's 0x62 interface, as in the
[Anatase EC driver](https://github.com/anatase-org/patchwork/blob/anatase-7.2/drivers/platform/arm64/samsung-galaxybook-ec.c).
The hotkey can cycle the [Plasma energy profiles](../../userspace/power-profiles/).

Build from this directory against the headers and build files of the target
kernel. An installation using DKMS can use the included `dkms.conf`.

```sh
make -C "/lib/modules/$(uname -r)/build" M="$PWD" modules
```

If managing modules manually, use the kernel's `updates/` directory and
`depmod` so the distribution module remains available. When loading from an
initramfs, make sure it contains the matching module. The fan remains under EC
control with the default module settings.
