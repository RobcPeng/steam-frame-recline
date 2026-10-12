# Recline Mode for Steam Frame

A dashboard tab for adjusting the VR horizon while reclining. Install it once on the Frame, open it alongside your game, and use **Recenter here**, **Tilt**, **Height**, or **Reset upright**. Closing Recline restores unadjusted tracking.

**Experimental.** Native SUPERHOT VR passed an observed 5° tilt/reset check with stable display and aligned controllers. Recline was also observed working in native Moss. Full-range comfort, every game, and PC-streamed VR are not verified. The new passthrough guard has passed simulated transitions and its live visibility flag was observed, but its visual camera transition still needs acceptance testing.

## Install on the Frame

Run these commands on the Steam Frame, as its normal Steam user. No sudo and no per-game launch changes are needed. Installation restarts SteamVR, closing running VR games.

```sh
git clone https://github.com/RobcPeng/steam-frame-recline.git
cd steam-frame-recline
./install.sh --check
./install.sh
```

Open **Recline Mode** from Applications. To put it in your Steam library, add `~/.local/bin/recline-mode` as a non-Steam game. An existing shortcut to that path continues to work.

The bundled ARM64 binaries were built and tested on Steam Frame with SteamVR 2.18.2. The installer checks checksums and shared-library dependencies. Camera protection currently recognizes one inspected SteamVR camera-interface implementation; on another build, tilt stays paused and the panel reports that an update is needed. This is not a general Linux PC installer.

## Use

1. Start a VR game, then open Recline Mode.
2. Lean back into your desired position and choose **Recenter here**. There is a three-second delay before capture.
3. Return to the game. Keep Recline open in its dashboard tab.
4. Adjust tilt by 1° or height by 5 cm as needed. Tilt is limited to 0–90°; height to ±1 m.
5. Choose **Reset upright** or close the app to stop applying the adjustment.

The camera/menu guard suspends the shared transform when it detects passthrough or the dashboard. Your chosen angle stays saved while paused. Loss of tracking or the app heartbeat clears the active adjustment, requiring a new action; it does not silently resume after a tracking failure. Controls are disabled while tracking is unavailable.

For an immediate reset from a terminal:

```sh
~/.local/bin/recline-mode restore
```

## Remove or update

```sh
./install.sh --uninstall
```

Uninstall restores the launchers and prior Recline driver registration saved before the first installation, then restarts SteamVR. Files are installed in your home directory. The installer does not edit room calibration, game files, saves, or system packages.

To update, run `git pull --ff-only` in the cloned directory and run `./install.sh` again. The original uninstall snapshot is retained.

## Compatibility and limitations

- This adjusts shared headset/controller tracking through a SteamVR driver callback. Consumers that bypass that callback may not receive it. Device-local installation does **not** establish PC-streaming support.
- Passthrough is kept upright by suspending the entire adjustment, including the game, while the camera is visible. It does not independently rotate the game behind translucent passthrough.
- Camera detection uses read-only private SteamVR methods guarded by exact implementation checks. SteamVR updates may disable this feature until it is reviewed again. Detection is polled, so transitions are not guaranteed to be frame-perfect.
- Moss has an intermittent Android startup crash in its window-animation/Binder path that was reproduced without either Recline implementation. A separate game-local animation workaround passed three launches during testing. That repair is not included in this installer.
- Do not combine this with the earlier Moss-only OpenXR Recline prototype. This release contains only the shared driver and dashboard.
- There is no automatic tilt on boot, and no persistent keep-awake change.

## Build and test

On Linux ARM64 with a C++17 compiler, CMake, pkg-config, SDL2 and SDL2_ttf development files, and SteamVR at `/opt/steamvr`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Tests cover head-pivot transforms, yaw and automatic angle capture, height, controller alignment, lease expiration, reset, full and bounded commands, malformed camera state, pause/resume, callback retention, and installer restoration. The driver fixture runs with a temporary data directory and does not connect to actual tracking hardware.

To package your own build, copy `build/recline-mode` and `build/driver_recline_shared.so` into `payload/`, then regenerate `payload/SHA256.json` with their SHA-256 hashes before installing.

Implementation details are in [docs/architecture.md](docs/architecture.md). Third-party headers retain their [license notices](THIRD_PARTY_NOTICES.md).
