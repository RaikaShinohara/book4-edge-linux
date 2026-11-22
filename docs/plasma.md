# Plasma on the Galaxy Book4 Edge

- [Power profiles](../userspace/power-profiles/) add the three standard
  profiles to Plasma. Fn+F11 requires the updated
  [Samsung EC module](../driver/samsung-galaxybook-ec/). The hotkey was tested
  across a full three-profile cycle.
- [Oryon Monitor](../userspace/oryon-monitor/) provides a live hardware and
  process panel on Meta+Esc.
- The optional [DP-1 recovery service](../userspace/display-recovery/)
  addresses a black external screen after some hub reconnections.
- On the internal eDP panel, Plasma's **Prefer color accuracy** setting
  avoids the gamma-table change observed when moving between 95% and 100%
  brightness with **Prefer efficiency**. Change only the internal display
  in Display Configuration. This behavior was checked on Plasma Wayland.
- On Chrome for Linux, **Use system title bar and borders** selects Plasma's
  window buttons. The corresponding preference is
  `browser.custom_chrome_frame=false`; Chrome must be fully closed before
  editing its profile file. The vendor documentation for native frames is
  in the [Chromium source](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/common/pref_names.h).
