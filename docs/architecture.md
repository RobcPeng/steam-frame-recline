# Architecture

The dashboard publishes commands and a 200 ms heartbeat in the user's OpenVR configuration directory. A registered server driver transforms the world-from-driver pose for headset and controllers around the captured head position. Raw tracking is cached before transformation, preventing recenter feedback. Local poses, velocities, and acceleration remain unchanged for prediction.

The backend replaces one `IVRServerDriverHost_006` virtual-dispatch pointer using atomic compare/exchange. It does not patch executable instructions. The module remains pinned for the server process lifetime; cleanup disables the transform without freeing a callback that another driver could still use. Restart SteamVR to remove it. This assumes the inspected Linux ARM64 interface layout and no competing patch of the same slot/page.

Commands contain sequence, action, pitch, height, Unix heartbeat time, immutable monotonic issue time, and camera/menu inhibition flag. New adjustments must arrive within 500 ms. A missing or malformed camera flag inhibits full-mode tilt. The panel polls visibility every 20 ms and publishes inhibition transitions immediately. Dashboard, camera visibility or rendering flags inhibit the transform; unrecognized camera code fails closed. The same selected transform resumes when inhibition clears, provided leases and tracking remain valid.

The driver expires a stopped panel after one second; the tracking callback also checks a 500 ms service lease. Expiry latches the adjustment off. A renewed heartbeat alone cannot reactivate an expired command. Tracking loss consumes new commands rather than postponing them until tracking returns. Reset works without valid tracking. A bounded `test` command has an independent two-second deadline.

The camera getter guard identifies the inspected implementations in `vrclient.so` SHA256 `19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0`. No Valve runtime binary is distributed. The installer ships our binaries and headers under their respective licenses.

Observed native-game checks do not establish all-consumer coverage, visual behavior at every angle, passthrough transition quality, or streamed VR compatibility. Changes to this boundary require fresh device testing.
