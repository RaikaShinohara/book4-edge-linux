# Windows-style touchpad gestures on Plasma Wayland

Tested on the NP750XQA with the Zinitix `14E5:650E` touchpad, KWin 6.8.3 and
the [InputActions KWin plugin v0.9.2.1](https://github.com/InputActions/kwin/tree/v0.9.2.1).
Follow the [InputActions installation guide](https://wiki.inputactions.org/main/getting-started/installation/kwin.html)
to build the plugin for your installed KWin. Rebuild it after a KWin version
change and restart the Plasma session to load the new build.

| Gesture | Plasma action |
| --- | --- |
| Three-finger tap | Open KRunner |
| Four-finger tap | Open notifications with Meta+N |
| Three-finger horizontal swipe | Browse the window switcher and select on release |
| Four-finger horizontal swipe | Switch virtual desktops using KWin's animation |
| Three- or four-finger swipe up | Open Overview or restore windows after showing the desktop |
| Three- or four-finger swipe down | Show the desktop |

Use Plasma's Touchpad settings for tap-to-click, tap-and-drag, the lower-right
button area, and two-finger scrolling. Pinch-to-zoom is handled by compatible
apps. Set up at least two virtual desktops in System Settings for the
four-finger horizontal swipe.

Install the Overview helper and the rule that gives the active local session
access to raw events from the internal touchpad. The rule matches the tested
device by its name and I²C controller. Other touchpad models need their own
device name in both the rule and the profile's `device_rules`.

```sh
sudo install -Dm755 bookedge-touchpad-overview /usr/local/bin/bookedge-touchpad-overview
sudo install -Dm644 71-bookedge-touchpad.rules /etc/udev/rules.d/71-bookedge-touchpad.rules
sudo udevadm control --reload
grep -lFx 'hid-over-i2c 14E5:650E Touchpad' /sys/class/input/event*/device/name
```

Replace `eventN` with the event node printed above, then apply the rule.

```sh
sudo udevadm trigger --action=change --settle /sys/class/input/eventN
```

Assign Meta+N to the Plasma Notifications widget. If the widget already has a
different shortcut, use its key sequence in the four-finger tap action. On
Plasma builds where the tray widget has no shortcut page, the corresponding
setting is `global=Meta+N` in its `[Shortcuts]` subgroup in
`plasma-org.kde.plasma.desktop-appletsrc`. The parent applet group contains
`plugin=org.kde.plasma.notifications`. Stop `plasma-plasmashell.service` with
`systemctl --user` before editing that file and start it again afterward.

Install the profile if you're setting up InputActions for the first time.
With an existing profile, merge the entries into its `device_rules` and
`touchpad.gestures` lists.

```sh
install -Dm644 config.yaml "${XDG_CONFIG_HOME:-$HOME/.config}/inputactions/config.yaml"
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect kwin_gestures
kwriteconfig6 --file kwinrc --group Plugins --key kwin_gesturesEnabled --type bool true
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect kwin_gestures
qdbus6 org.inputactions / org.inputactions.issues
```

InputActions' [example gestures](https://wiki.inputactions.org/main/example-triggers.html)
document the Alt+Tab behavior used for the window switcher.
