# Power profiles on the NP750XQA

On Arch Linux ARM, install `tuned-ppd`, then run `sudo ./install.sh` from this directory. The script sets up three TuneD profiles, verifies their frequency limits through the Power Profiles D-Bus API and selects Balanced. Existing TuneD settings are backed up under `/var/lib/bookedge-energy/`.

| Profile | CPU limit | Adreno limit |
| --- | --- | --- |
| Power Saver | 1.92 GHz | 550 MHz |
| Balanced | 2.976 GHz | 940 MHz |
| Performance | 3.245 GHz (cores 0–3), 3.418 GHz (cores 4–7) | 1,107 MHz |

The CPU uses `schedutil` with boost enabled and Adreno uses `simple_ondemand`. All three modes scale to their idle frequencies. The EC controls the fan automatically in every mode; none of these profiles programs a fan curve.

Plasma offers these profiles in the battery applet. If PowerDevil was already running before TuneD was installed, run:

```sh
systemctl --user restart plasma-powerdevil.service
```

Meta+B cycles profiles in Plasma. With the [updated EC driver](../../driver/samsung-galaxybook-ec/) installed, Fn+F11 uses the same Plasma action. The driver reports the Samsung performance-mode packet as `KEY_BATTERY`. It preserves the normal F11 key and leaves the EC fan policy alone.

## Select the profile when plugging in the charger

The optional Plasma user service uses UPower events to select Performance
on AC and Power Saver on battery. A manual selection can stay active until
the power source changes. The service expects the three profiles to be
available in Plasma through the setup above and requires PyGObject.

~~~sh
install -Dm644 bookedge-power-source-profile ~/.local/bin/bookedge-power-source-profile
install -Dm644 bookedge-power-source-profile.service \
    ~/.config/systemd/user/bookedge-power-source-profile.service
systemctl --user enable --now bookedge-power-source-profile.service
~~~
