# Playing CitixChase on your local network

Use `PackageLAN/Windows/Citix.exe` on both computers. Copy the entire Windows folder together.

1. Connect both computers to the same Wi-Fi or wired local network.
2. One player gives their room a name and clicks **Host Game**.
3. The friend clicks **Find Nearby Games**, chooses the named room showing **1/2 players**, then **Join**.
4. Both players click **Ready** after city verification completes. The existing roles and match rules apply.

The default room name is **CitixRoom1**; you can change it before hosting.

If Windows asks, allow the game on your private network. If no rooms appear, keep the host open and search again; guest Wi-Fi can isolate devices.

For direct connection, expand **Advanced**. The host enters this computer's local IP address (optionally followed by a port, such as `192.168.1.20:7777`) and clicks **Host IP**. The friend enters that same address and clicks **Join IP**. Leave the host address blank to use the detected local address and port 7777. Hosting requires an address belonging to this computer; entering another computer's address is for joining.

ESC opens saved settings: render scale 30%-110%, and maximum FPS 30/60/120/144/240/Unlimited. Use Automatic restores the device recommendation and keeps your FPS cap.

Low now uses minimum effects and shorter distant visibility. Its recommended render scale is 50%; a manually chosen render scale remains independent of the preset. Smoke and Ice Wave HUD text stays at native viewport resolution, with solid blue available charges and muted empty rings.

Internet room codes/Epic Online Services are deferred. This build uses LAN discovery and Unreal session APIs.

Validation performed on this computer is recorded in `Docs/LAN_SETTINGS_DRIFT_REVIEW.md`. A real two-computer LAN test remains required before claiming that environment verified.
