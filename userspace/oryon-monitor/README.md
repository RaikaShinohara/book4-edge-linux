# Oryon Monitor

Spanish-language Qt 6 monitor for the Galaxy Book4 Edge X1P-42-100. It shows the eight Oryon cores, Adreno frequency and estimated load, temperatures, battery, storage, network and a sortable process list. CPU usage is per core; Linux reports one frequency per cpufreq policy. GPU load is estimated from accessible DRM clients.

For Arch Linux ARM and Plasma Wayland, install the Qt 6 base/Wayland, KGlobalAccel and KWindowSystem development files and the C++ build tools. From this directory:

```sh
source_dir="$PWD"
build_dir=$(mktemp -d)
cd "$build_dir"
qmake6 "$source_dir/oryon-monitor.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./oryon-monitor --export-icon oryon-monitor.png

sudo install -Dm755 oryon-monitor /usr/local/bin/oryon-monitor
sudo install -Dm644 oryon-monitor.png /usr/local/share/pixmaps/oryon-monitor.png
sudo install -Dm644 "$source_dir/bookedge-oryon-monitor.desktop" /usr/local/share/applications/bookedge-oryon-monitor.desktop
sudo install -Dm644 "$source_dir/oryon-monitor.service" /etc/systemd/user/oryon-monitor.service
systemctl --user daemon-reload
systemctl --user enable --now oryon-monitor.service
```

Open it from the menu or press **Meta+Esc** to show and hide it. Sampling pauses while the window is hidden or minimized.
