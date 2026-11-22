# Optional DP-1 recovery on Plasma Wayland

One tested USB-C hub could still leave DP-1 black after reconnecting. The kernel detected the monitor, but the connector stayed disabled while KScreen marked it enabled. Reapplying the same saved mode recovered the picture without moving the cable. This service automates that step while the discrepancy is present. It requires Python 3 and `kscreen-doctor`.

```sh
sudo install -Dm755 bookedge-display-recovery /usr/local/bin/bookedge-display-recovery
sudo install -Dm644 bookedge-usbc-display-recovery.service /etc/systemd/user/bookedge-usbc-display-recovery.service
systemctl --user daemon-reload
systemctl --user enable --now bookedge-usbc-display-recovery.service
```
